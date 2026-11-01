#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Every synthetic set is drawn from std::mt19937_64 with this seed, which means every machine times the same numbers.
constexpr uint64_t kSeed = 20261031;

enum class IntegerType
{
    none,
    u32,
    u64,
    i32,
    i64
};

// The position of one text inside Dataset::characters.
struct Span
{
    uint32_t begin;
    uint32_t end;
};

// A row of the result tables. Number sets hold texts and their values, while integer sets hold nothing but integers.
struct Dataset
{
    std::string           name;
    std::string           characters; // every text, each one followed by '\n'
    std::vector<Span>     texts;
    std::vector<double>   doubles; // each text as read by std::from_chars
    std::vector<float>    floats; // each text read as a float, or empty if any of the texts is outside the float range
    std::vector<uint64_t> integers;
    IntegerType           integerType = IntegerType::none;
    std::string           note; // what was left out, if anything, for the output to report

    size_t Count() const
    {
        return integerType == IntegerType::none ? texts.size() : integers.size();
    }
};

std::vector<Dataset> SyntheticDatasets( size_t count );
std::vector<Dataset> IntegerDatasets( size_t count );

// Loads the JSON-grammar numbers in any text file, such as JSON, OGEX and the like. Returns false and puts a message in
// `error` if the file can't be read.
bool LoadFile( const char* path, Dataset& dataset, std::string& error );
