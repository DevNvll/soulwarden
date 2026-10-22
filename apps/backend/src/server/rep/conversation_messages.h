#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// The messages of the conversation of a person (stage 2.5c, docs/systems/persons.md, protocol notes 4.55; research npc-conversations.md
// Q2). Each body has the 16-byte component prefix of the Conversation component of the player (target u64 BE, G u64 BE), then:
//   3158 ConversationComponentServerFacet_RequestConversationTopicFromClient (caller 0x376E530, sender 0x3722AE0, reader 0x26FEC60 through
//        wrapper 0x3707E90): topic:u32 BE (the CRC-32 of the lower-case ConversationTopicId). The client sends it when its player selects
//        a topic of the open conversation (option 2 of the dispatcher).
//   5397 ConversationComponentServerFacet_OnFinishedConversationTopicsFromClient (close 0x37622D0, sender 0x3722540, writer 0x3701DF0,
//        reader 0x3701E90 through wrapper 0x37080A0): count:size (10 at most), then count topic:u32 BE. The client sends it at the close of
//        the conversation.
// The server sends no answer to either message (no answer is known).

namespace soulwarden::backend::server::rep {

inline constexpr std::uint32_t kTypeConversationTopic = 3158;
inline constexpr std::uint32_t kTypeConversationTopicsFinished = 5397;
/** The Conversation component (entity RootPlayer) of the player slice, by its name in slice-ids.json. */
inline constexpr const char* kConversationComponentName = "RootPlayer/7447fb63-fa77-44eb-8605-6dbab2ba7a00";
/** The writer 0x3701DF0 writes ten topics at most. */
inline constexpr std::size_t kFinishedTopicsLimit = 10;

struct ConversationTopics {
    std::uint64_t target{0};
    std::uint64_t entity{0};
    std::vector<std::uint32_t> topics;
};

/** @return The body of 3158 (one topic), or no value when it does not have the form. */
[[nodiscard]] std::optional<ConversationTopics> read_conversation_topic(std::span<const std::uint8_t> body);
/** @return The body of 5397 (0 to 10 topics), or no value when it does not have the form. */
[[nodiscard]] std::optional<ConversationTopics> read_finished_topics(std::span<const std::uint8_t> body);
/** @return The body of 3158 (tests). */
[[nodiscard]] std::vector<std::uint8_t> conversation_topic_body(std::uint64_t target, std::uint64_t entity, std::uint32_t topic);
/** @return The body of 5397 (tests). */
[[nodiscard]] std::vector<std::uint8_t> finished_topics_body(std::uint64_t target, std::uint64_t entity, const std::vector<std::uint32_t>& topics);

} // namespace soulwarden::backend::server::rep
