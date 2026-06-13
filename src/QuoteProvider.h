#ifndef QUOTE_PROVIDER_H
#define QUOTE_PROVIDER_H

#include <random>
#include <string>
#include <vector>

class QuoteProvider {
public:
    QuoteProvider();

    std::string randomQuote();
    std::size_t randomIndex();
    const std::vector<std::string>& quotes() const;

private:
    std::vector<std::string> quotes_;
    std::mt19937 rng_;
};

#endif
