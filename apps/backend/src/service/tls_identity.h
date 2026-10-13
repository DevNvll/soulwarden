#pragma once

#include <string>

namespace soulwarden::backend::service {

/**
 * Local transport identity: one RSA key and one self-signed certificate.
 * The game hook adds the certificate to the client trust list. The key stays in the
 * backend data directory.
 */
struct TlsIdentity {
    std::string certificatePem;
    std::string privateKeyPem;
};

/** Loads the identity from a directory, or makes it on the first run. @return False on failure. */
[[nodiscard]] bool load_or_create_identity(const std::string& directory, TlsIdentity& identity);

} // namespace soulwarden::backend::service
