#!/usr/bin/env python3
"""Check the Steam identity path of one captured run (tools/dev-run.sh keeps the captures).

Usage: check_identity.py [DATA_DIR] [STEAM_DIR]

DATA_DIR has captures/http, captures/rep, and soulwarden.sqlite3 (default: the data directory of
the development tools, tools/dev-env.sh).
Run it after the client was in the world, or at the main menu for the HTTP part only.
A run can have the sessions of more than one account. The tool compares each registration only
with the token request, the login queue request, and the database row of its own persona, and
it prints one result for each session. The last two lines are the result of the run.
The game server keeps the captures of each session in its own folder (captures/rep/s0001,
...). The tool reads the registration request of each session: a run in which the client
left the world and entered it again has two.

The tool compares the Steam id at each step of the login path:

  x-amzn-playerid          the value that the client reads from the hook (GetSteamID)
  token request            Steam id in the local ticket (platformAuth.ticket)
  token reply              platformAccount.identityId
  login queue request      Steam id in the local ticket (SteamAuthTicket)
  login queue reply        Token.SteamUserId
  registration request     SteamUserId of the login token, Steam id of the client, and the
                           Steam id in the local ticket ("steam|" + hexadecimal text)
  accounts.steam_id        the account row of the persona

The captures and the database have the full Steam id and the persona name of the player.
The tool prints only the last 4 digits of a Steam id and the size of a persona name.
"""

from __future__ import annotations

import base64
import json
import re
import sqlite3
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import dev_env  # noqa: E402
from client_msgs import messages, read_size  # noqa: E402

TYPE_REGISTRATION_V3 = 19
TICKET_MARKER = "SOULWARDEN-LOCAL-TICKET"
TICKET_VERSION = "1"
INDIVIDUAL_ACCOUNT_BITS = 0x01100001


def masked(steam_id: str | None) -> str:
    return f"...{steam_id[-4:]}" if steam_id else "(none)"


def is_steam_id(text: str) -> bool:
    return len(text) == 17 and text.isdigit() and int(text) >> 32 == INDIVIDUAL_ACCOUNT_BITS


def ticket_identity(hex_text: str) -> tuple[str, bytes] | None:
    """@return The Steam id and the persona name of a local ticket, or None."""
    if hex_text.startswith("steam|"):
        hex_text = hex_text[6:]
    try:
        fields = bytes.fromhex(hex_text).decode("latin-1").split(";")
        name = bytes.fromhex(fields[3]) if len(fields) == 4 else b""
    except ValueError:
        return None
    if len(fields) != 4 or fields[0] != TICKET_MARKER or fields[1] != TICKET_VERSION or not is_steam_id(fields[2]):
        return None
    return fields[2], name


class Capture:
    """One HTTP capture file of the backend."""

    def __init__(self, path: Path):
        self.name = path.name
        text = path.read_text(encoding="utf-8", errors="replace")
        head, _, rest = text.partition("\nREQUEST BODY ")
        self.headers = {key.lower(): value for key, value in re.findall(r"^([\w-]+): (.*)$", head, re.M)}
        request, _, reply = rest.partition("\nREPLY ")
        self.request = self._json(request)
        self.reply = self._json(reply)

    @staticmethod
    def _json(part: str):
        _, _, body = part.partition("\n")
        try:
            return json.loads(body.strip() or "null")
        except ValueError:
            return None


def find_capture(directory: Path, pattern: str) -> Capture | None:
    files = sorted((directory / "http").glob(pattern))
    return Capture(files[0]) if files else None


def find_header(directory: Path, name: str) -> Capture | None:
    """@return The first capture with this request header. A long path cuts the file name."""
    for path in sorted((directory / "http").glob("*.txt")):
        capture = Capture(path)
        if name in capture.headers:
            return capture
    return None


def token_persona(token: str) -> str:
    """@return The claim personaId of an access token."""
    try:
        payload = token.split(".")[1]
        return json.loads(base64.urlsafe_b64decode(payload + "=" * (-len(payload) % 4))).get("personaId", "")
    except (IndexError, ValueError):
        return ""


class Reader:
    def __init__(self, data: bytes):
        self.data = data
        self.position = 0

    def u8(self) -> int:
        self.position += 1
        return self.data[self.position - 1]

    def u32(self) -> int:
        self.position += 4
        return struct.unpack_from(">I", self.data, self.position - 4)[0]

    def text(self) -> str:
        size, self.position = read_size(self.data, self.position)
        self.position += size
        if self.position > len(self.data):
            raise IndexError("short read")
        return self.data[self.position - size:self.position].decode("latin-1")


def registration(body: bytes) -> dict[str, str]:
    """Identity fields of RegistrationRequestV3Msg (client writers 0x7CE860, 0x7D54A0, 0x7D5F30)."""
    reader = Reader(body)
    reader.u32()
    count, reader.position = read_size(body, reader.position)
    for _ in range(count):
        reader.u32()
        reader.text()
    reader.text()
    result = {}
    for name in ("signature", "repAddress", "worldId", "characterId", "personaId"):
        result[name] = reader.text()
    reader.u32()  # Steam application id of the token.
    result["tokenSteamId"] = reader.text()
    for name in ("channelId", "ticketId"):
        result[name] = reader.text()
    reader.text()
    reader.text()
    reader.u32()  # Issue time.
    reader.u32()  # Generate time.
    reader.u32()  # Account age.
    reader.text()
    reader.u32()  # Token version.
    reader.u8()
    reader.text()
    if reader.u8():
        reader.u8()
    reader.text()
    reader.text()
    reader.text()
    reader.u32()
    reader.u8()
    reader.u32()  # Steam application id of the client.
    result["clientSteamId"] = reader.text()
    result["ticket"] = reader.text()
    return result


def find_captures(directory: Path, pattern: str) -> list[Capture]:
    return [Capture(path) for path in sorted((directory / "http").glob(pattern))]


class Identity:
    """The values of one account (persona id) at each step of the login path."""

    def __init__(self) -> None:
        self.steam_ids: dict[str, str | None] = {}
        self.personas: dict[str, str] = {}

    def result(self, persona: str) -> tuple[bool, bool]:
        """@return (the Steam ids are equal, the persona ids are equal)."""
        steam = len(set(self.steam_ids.values())) == 1 and None not in self.steam_ids.values()
        return steam, set(self.personas.values()) <= {persona}


def main() -> None:
    data = Path(sys.argv[1]) if len(sys.argv) > 1 else dev_env.data_dir()
    steam = Path(sys.argv[2]) if len(sys.argv) > 2 else Path.home() / ".local/share/Steam"
    captures = data / "captures"
    # One identity for each account of the run. A run can have the sessions of more than one
    # account: each registration is compared only with the token and the database row of its
    # own persona.
    accounts: dict[str, Identity] = {}

    def account(persona: str) -> Identity:
        return accounts.setdefault(persona, Identity())

    tokens = [capture for capture in find_captures(captures, "*-POST-tokenservice_*_tokens.txt")
              if isinstance(capture.reply, dict) and isinstance(capture.request, dict)]
    if not tokens:
        sys.exit(f"no token request in {captures}/http")
    for token in tokens:
        persona = token.reply["account"]["personaId"]
        identity = ticket_identity(token.request["platformAuth"]["ticket"])
        values = account(persona)
        values.steam_ids["local ticket of the token request"] = identity[0] if identity else None
        values.steam_ids["token reply platformAccount.identityId"] = token.reply["platformAccount"]["identityId"]
        values.personas["token reply account.personaId"] = persona
        print(f"token request of persona {persona}: {'local ticket' if identity else 'NO local ticket'}"
              + (f", persona name of {len(identity[1])} bytes" if identity else ""))

    for path in sorted((captures / "http").glob("*.txt")):
        capture = Capture(path)
        if "x-amzn-playerid" in capture.headers:
            persona = token_persona(capture.headers.get("x-amzn-token", ""))
            values = account(persona)
            values.steam_ids["x-amzn-playerid of the client"] = capture.headers["x-amzn-playerid"]
            values.personas["x-amzn-token of the client"] = persona
    for login_info in find_captures(captures, "*_prod_game_getlogininfo*.txt"):
        if isinstance(login_info.reply, dict):
            persona = token_persona(login_info.headers.get("x-nw-auth", ""))
            account(persona).personas["x-nw-auth of the character list request"] = persona
            names = [character["Name"] for character in login_info.reply["LoginInfoList"]["Characters"]]
            print(f"character list of persona {persona}: {names}")
    queues = [capture for capture in find_captures(captures, "*_login_queue_v2.txt") if isinstance(capture.reply, dict)]
    for queue in queues:
        login_token = queue.reply["LoginQueueResponse"]["Token"]
        identity = ticket_identity(queue.request["LoginQueueRequest"]["SteamAuthTicket"])
        values = account(login_token["PersonaId"])
        values.steam_ids["local ticket of the login queue request"] = identity[0] if identity else None
        values.steam_ids["login token SteamUserId"] = login_token["SteamUserId"]
        values.personas["x-nw-auth of the login queue request"] = token_persona(queue.headers.get("x-nw-auth", ""))
        values.personas["login token PersonaId"] = login_token["PersonaId"]
    if not queues:
        print("login queue: no capture (the client did not press Play)")

    database = data / "soulwarden.sqlite3"
    if database.exists():
        connection = sqlite3.connect(f"file:{database}?mode=ro", uri=True)
        print("schema version:", connection.execute("PRAGMA user_version").fetchone()[0])
        for row in connection.execute(
                "SELECT persona_id, account_id, steam_id, length(CAST(persona_name AS BLOB)), created_date, last_login_date "
                "FROM accounts ORDER BY created_date"):
            characters = [name for (name,) in connection.execute(
                "SELECT name FROM characters WHERE persona_id=?1 AND deleted_date IS NULL ORDER BY created_date", (row[0],))]
            print(f"account {row[0]} / {row[1]}: Steam id {masked(row[2])}, persona name of {row[3]} bytes, "
                  f"created {row[4]}, last login {row[5]}, characters {characters}")
            if row[0] in accounts:
                accounts[row[0]].steam_ids["accounts.steam_id of the persona"] = row[2]

    # Each registration request: one result for each session. The values of the registration
    # are compared with the values of its own persona.
    results: list[tuple[bool, bool]] = []
    for name, type_index, body in messages(captures):
        if type_index != TYPE_REGISTRATION_V3:
            continue
        fields = registration(body)
        identity = ticket_identity(fields["ticket"])
        # The session folder of the file, or "registration" for a run with no session folders.
        source = f"session {name.split('/')[0]}" if "/" in name else "registration"
        persona = fields["personaId"]
        print(f"{source}: registration request {name}: character {fields['characterId']}, persona {persona}, "
              f"ticket text of {len(fields['ticket'])} characters")
        session = Identity()
        known = accounts.get(persona)
        if known is None:
            print(f"{source}: no token request of persona {persona} in the captures")
        else:
            session.steam_ids.update(known.steam_ids)
            session.personas.update(known.personas)
        session.steam_ids["registration: SteamUserId of the login token"] = fields["tokenSteamId"]
        session.steam_ids["registration: Steam id of the client"] = fields["clientSteamId"]
        session.steam_ids["registration: local ticket"] = identity[0] if identity else None
        session.personas["registration: persona id of the login token"] = persona
        for label, value in session.steam_ids.items():
            print(f"{source}: {label}: {masked(value)}")
        for label, value in session.personas.items():
            print(f"{source}: {label}: {value}")
        result = session.result(persona) if known is not None else (False, False)
        results.append(result)
        print(f"{source}: Steam ids: {'equal' if result[0] else 'DIFFERENT'}; persona ids: {'equal' if result[1] else 'DIFFERENT'}")
    if not results:
        print("registration request: no capture (the client did not connect to the game server)")
        # The HTTP part only: one result for each account.
        for persona, values in accounts.items():
            for label, value in values.steam_ids.items():
                print(f"persona {persona}: {label}: {masked(value)}")
            for label, value in values.personas.items():
                print(f"persona {persona}: {label}: {value}")
            results.append(values.result(persona))

    # The last two lines are the result of the run: each session (or account) must agree.
    print("Steam ids:", "equal" if results and all(result[0] for result in results) else "DIFFERENT")
    print("persona ids:", "equal" if results and all(result[1] for result in results) else "DIFFERENT")

    login_file = steam / "config/loginusers.vdf"
    if login_file.exists():
        users = re.findall(r'"(\d{17})"\s*\{', login_file.read_text(encoding="utf-8", errors="replace"))
        for persona, values in accounts.items():
            for name in ("x-amzn-playerid of the client", "token reply platformAccount.identityId"):
                if name in values.steam_ids:
                    print(f"persona {persona}: {name} is an account of the Steam installation:",
                          values.steam_ids[name] in users)
    else:
        print(f"no {login_file}")


if __name__ == "__main__":
    main()
