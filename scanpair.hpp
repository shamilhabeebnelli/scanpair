#pragma once
#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace sp {
struct Page {
    int width{}, height{};
    std::vector<unsigned char> pixels;
    std::array<double, 1024> ink{};
    double coverage{};
};
struct Pair { int a{-1}, b{-1}; double similarity{}; bool ambiguous{}; };
Page read_pgm(const std::filesystem::path& path);
void describe(Page& p);
double similarity(const Page& a, const Page& b);
std::vector<Pair> align(const std::vector<Page>& a, const std::vector<Page>& b);
std::string escape(const std::string& s);
std::string thumbnail(const Page& p);
void report(const std::filesystem::path& out, const std::string& a_name,
            const std::string& b_name, const std::vector<Page>& a,
            const std::vector<Page>& b, const std::vector<Pair>& pairs);
}
