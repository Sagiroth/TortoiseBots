#ifndef _PlayerbotTextMgr_H
#define _PlayerbotTextMgr_H

#include "Common.h"
#include "PlayerbotAIBase.h"

#define BOT_TEXT(name) sPlayerbotTextMgr.GetBotText(name)
#define BOT_TEXT2(name, replace) sPlayerbotTextMgr.GetBotText(name, replace)

struct BotTextEntry
{
    BotTextEntry(std::string name, std::string text, std::map<int32, std::string> text_locales) : m_name(name), m_text(text), m_text_locales(text_locales) {}
    std::string m_name;
    std::string m_text;
    std::map<int32, std::string> m_text_locales;
};

class PlayerbotTextMgr
{
    public:
        PlayerbotTextMgr();
        virtual ~PlayerbotTextMgr();
        static PlayerbotTextMgr& instance()
        {
            static PlayerbotTextMgr instance;
            return instance;
        }

	public:
        static void ReplaceAll(std::string& str, const std::string& from, const std::string& to);
        static void ReplacePlaceholders(std::string& text, const std::map<std::string, std::string>& placeholders);
        static std::string GetReplacePlaceholders(const std::string& text, const std::map<std::string, std::string>& placeholders) { std::string retText = text; ReplacePlaceholders (retText, placeholders); return retText;  }

        std::string GetBotText(std::string name, std::map<std::string, std::string> placeholders);
        std::string GetBotText(std::string name);
        // Use localized/database text when it exists, otherwise preserve the
        // caller's behavior without making SQL-backed bot text mandatory.
        std::string GetBotTextOrDefault(std::string name, std::string fallback, std::map<std::string, std::string> placeholders)
        {
            std::string text = GetBotText(name);
            if (!text.empty())
            {
                ReplacePlaceholders(text, placeholders);
                return text;
            }

            ReplacePlaceholders(fallback, placeholders);
            return fallback;
        }
        void LoadBotTexts();

        int32 GetLocalePriority();
        void AddLocalePriority(int32 locale);
        void ResetLocalePriority();

    private:
        std::map<std::string, std::vector<BotTextEntry>> botTexts;
        uint32 botTextLocalePriority[MAX_LOCALE];
};

#define sPlayerbotTextMgr PlayerbotTextMgr::instance()

#endif
