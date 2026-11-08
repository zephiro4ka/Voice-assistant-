#pragma once
// Живое общение без ИИ: «как дела», «что делаешь», шутки, комплименты, поддержка.
// Срабатывает на короткие разговорные фразы; длинные вопросы и задачи уходят ИИ.

#include <optional>
#include <string>

namespace Chatter {
    std::optional<std::string> Reply(const std::string& text);

    // Ответ, когда ИИ не запущен, а фраза не похожа ни на команду, ни на болтовню
    std::string NotUnderstood();
}
