#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace TortoiseBots {

// Every chat line a bot hears is copied on the sender's thread and parsed by
// PlayerbotAI::HandleBotOutgoingPacket on the world thread, so a large pool in
// a busy channel would cost the world tick O(bots x lines). The packet hook
// cannot ask the AI whether it cares (no AI pointer off the world thread), so
// each AI publishes what it reads and the hook skips the copy of the rest:
//
//  - ignoreAllChat: AllowActivity() is false, the handler returns first thing
//    (only with activity throttling, an opt-in; never by default).
//  - onlyOrders: the bot reads party chat and whispers only, where its orders
//    come from; every other line is wanted only while it records chat.
//
// The AI republishes on change from its world-thread update, so the gate is at
// most one AI update old; a bot without a published gate gets every line.
struct BotChatGate
{
    bool ignoreAllChat = false;
    bool onlyOrders = false;
};

// The fields of an SMSG_MESSAGECHAT CHAT_MSG_WHISPER body the inbox needs,
// read the way ChatHandler::BuildChatPacket writes them (type, language,
// sender guid, text length, text, chat tag). The text is what the sender
// typed: BuildChatPacket wraps the text of a sender whose chat tag is
// CHAT_TAG_GM in "|c1049e6ff" ... "|r" (not for LANG_ADDON), and the parse
// takes that wrap off again.
constexpr uint8_t kChatTagGm = 3;                    // the core's CHAT_TAG_GM
constexpr uint32_t kChatLanguageAddon = 0xFFFFFFFF;  // the core's LANG_ADDON

struct WhisperChatLine
{
    uint32_t language = 0;
    uint64_t sender = 0;
    char const* text = nullptr;
    size_t textLength = 0;
};

// `data` / `size`: the whole SMSG_MESSAGECHAT body of a whisper (type,
// language, sender guid, text length, text, chat tag), as
// ChatHandler::BuildChatPacket writes it. False when the body is too short
// to hold them.
inline bool ParseWhisperChatLine(uint8_t const* data, size_t size, WhisperChatLine& line)
{
    if (!data || size < 1 + 4 + 8 + 4)
        return false;
    uint32_t language = 0;
    for (int i = 3; i >= 0; --i) // little-endian, as ByteBuffer writes it
        language = (language << 8) | data[1 + static_cast<size_t>(i)];
    line.language = language;
    uint64_t sender = 0;
    for (int i = 7; i >= 0; --i) // little-endian, as ByteBuffer writes it
        sender = (sender << 8) | data[5 + static_cast<size_t>(i)];
    line.sender = sender;
    size_t const pos = 1 + 4 + 8 + 4; // type, language, sender, text length
    line.text = reinterpret_cast<char const*>(data + pos);
    void const* textEnd = std::memchr(data + pos, 0, size - pos);
    line.textLength = textEnd ? static_cast<size_t>(static_cast<uint8_t const*>(textEnd) - (data + pos)) : size - pos;
    size_t const tagPos = pos + line.textLength + 1;
    if (textEnd && tagPos < size && data[tagPos] == kChatTagGm && language != kChatLanguageAddon)
    {
        static char const kGmOpen[] = "|c1049e6ff";
        static char const kGmClose[] = "|r";
        size_t const openLength = sizeof(kGmOpen) - 1, closeLength = sizeof(kGmClose) - 1;
        if (line.textLength >= openLength + closeLength && std::memcmp(line.text, kGmOpen, openLength) == 0 &&
            std::memcmp(line.text + line.textLength - closeLength, kGmClose, closeLength) == 0)
        {
            line.text += openLength;
            line.textLength -= openLength + closeLength;
        }
    }
    return true;
}

} // namespace TortoiseBots
