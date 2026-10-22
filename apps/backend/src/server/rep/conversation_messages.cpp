#include "server/rep/conversation_messages.h"

#include "server/hub/wire.h"

namespace soulwarden::backend::server::rep {

std::optional<ConversationTopics> read_conversation_topic(std::span<const std::uint8_t> body) {
    hub::Reader reader(body);
    ConversationTopics message;
    message.target = reader.u64();
    message.entity = reader.u64();
    message.topics.push_back(reader.u32());
    if (!reader.ok() || reader.left() != 0) {
        return std::nullopt;
    }
    return message;
}

std::optional<ConversationTopics> read_finished_topics(std::span<const std::uint8_t> body) {
    hub::Reader reader(body);
    ConversationTopics message;
    message.target = reader.u64();
    message.entity = reader.u64();
    const std::uint32_t count = reader.size();
    if (!reader.ok() || count > kFinishedTopicsLimit) {
        return std::nullopt;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        message.topics.push_back(reader.u32());
    }
    if (!reader.ok() || reader.left() != 0) {
        return std::nullopt;
    }
    return message;
}

std::vector<std::uint8_t> conversation_topic_body(std::uint64_t target, std::uint64_t entity, std::uint32_t topic) {
    hub::Writer writer;
    writer.u64(target);
    writer.u64(entity);
    writer.u32(topic);
    return writer.data();
}

std::vector<std::uint8_t> finished_topics_body(std::uint64_t target, std::uint64_t entity, const std::vector<std::uint32_t>& topics) {
    hub::Writer writer;
    writer.u64(target);
    writer.u64(entity);
    writer.size(static_cast<std::uint32_t>(topics.size()));
    for (const std::uint32_t topic : topics) {
        writer.u32(topic);
    }
    return writer.data();
}

} // namespace soulwarden::backend::server::rep
