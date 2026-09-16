#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

/**
 * @brief Helpers for hand-building RIFF/WAV files in tests.
 *
 * Values are written in host byte order, which is little-endian on every
 * platform the core is built for — the same assumption the parser makes.
 */
namespace fixture {

/** @brief Growable byte buffer with chainable little-endian writers. */
class Bytes {
  public:
    Bytes &text(const std::string &s) {
        data_.insert(data_.end(), s.begin(), s.end());
        return *this;
    }

    template <typename T> Bytes &value(T v) {
        const auto *p = reinterpret_cast<const char *>(&v);
        data_.insert(data_.end(), p, p + sizeof(T));
        return *this;
    }

    Bytes &u16(uint16_t v) { return value(v); }
    Bytes &u32(uint32_t v) { return value(v); }

    Bytes &append(const Bytes &other) {
        data_.insert(data_.end(), other.data_.begin(), other.data_.end());
        return *this;
    }

    [[nodiscard]] const std::vector<char> &data() const { return data_; }
    [[nodiscard]] uint32_t size() const { return static_cast<uint32_t>(data_.size()); }

  private:
    std::vector<char> data_;
};

/** @brief A chunk: 4-char id, size, payload, and a pad byte if the payload is odd. */
inline Bytes chunk(const std::string &id, const Bytes &payload) {
    Bytes b;
    b.text(id).u32(payload.size()).append(payload);
    if (payload.size() % 2 != 0)
        b.value<uint8_t>(0);
    return b;
}

/** @brief A "fmt " chunk. `extraBytes` adds an extension block after the 16 standard bytes. */
inline Bytes fmtChunk(uint16_t audioFormat, uint16_t channels, uint32_t sampleRate,
                      uint16_t bitsPerSample, uint32_t extraBytes = 0) {
    const auto blockAlign = static_cast<uint16_t>(channels * (bitsPerSample / 8));
    Bytes payload;
    payload.u16(audioFormat)
        .u16(channels)
        .u32(sampleRate)
        .u32(sampleRate * blockAlign)
        .u16(blockAlign)
        .u16(bitsPerSample);
    for (uint32_t i = 0; i < extraBytes; ++i)
        payload.value<uint8_t>(0);
    return chunk("fmt ", payload);
}

/** @brief Wrap chunks in a RIFF/WAVE header with the correct RIFF size. */
inline Bytes riff(const Bytes &chunks) {
    Bytes b;
    b.text("RIFF").u32(4 + chunks.size()).text("WAVE").append(chunks);
    return b;
}

/** @brief A complete 16-bit PCM WAV. `extraChunks` sit between the "fmt " and "data" chunks. */
inline Bytes pcm16Wav(const std::vector<int16_t> &samples, uint16_t channels = 1,
                      uint32_t sampleRate = 44100, const Bytes &extraChunks = Bytes()) {
    Bytes data;
    for (int16_t s : samples)
        data.value(s);
    return riff(Bytes()
                    .append(fmtChunk(1, channels, sampleRate, 16))
                    .append(extraChunks)
                    .append(chunk("data", data)));
}

/** @brief A file in the system temp directory, removed when this object goes out of scope. */
class TempFile {
  public:
    explicit TempFile(const std::string &name)
        : path_((std::filesystem::temp_directory_path() / name).string()) {}

    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    TempFile(const TempFile &) = delete;
    TempFile &operator=(const TempFile &) = delete;

    [[nodiscard]] const std::string &path() const { return path_; }

    void write(const Bytes &bytes) const {
        std::ofstream out(path_, std::ios::binary | std::ios::trunc);
        out.write(bytes.data().data(), static_cast<std::streamsize>(bytes.size()));
    }

    [[nodiscard]] std::vector<char> read() const {
        std::ifstream in(path_, std::ios::binary);
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }

  private:
    std::string path_;
};

} // namespace fixture
