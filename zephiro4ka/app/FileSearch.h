#pragma once
// «Найди файл …»: своим поиском ASTRIX, через поиск Windows или через Everything ([search] files).

#include <optional>
#include <string>

namespace FileSearch {
    std::optional<std::string> Handle(const std::string& command); // не про файлы — nullopt
}
