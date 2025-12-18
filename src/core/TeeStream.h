#pragma once
#include <streambuf>
#include <ostream>

class TeeStreambuf : public std::streambuf {
public:
    TeeStreambuf(std::streambuf* left, std::streambuf* right)
        : left_(left), right_(right) {}

protected:
    int overflow(int ch) override {
        if (ch == EOF) {
            return EOF;
        }
        const int left_result = left_ ? left_->sputc(static_cast<char>(ch)) : ch;
        const int right_result = right_ ? right_->sputc(static_cast<char>(ch)) : ch;
        return (left_result == EOF || right_result == EOF) ? EOF : ch;
    }

    std::streamsize xsputn(const char* s, std::streamsize n) override {
        std::streamsize left_count = left_ ? left_->sputn(s, n) : n;
        std::streamsize right_count = right_ ? right_->sputn(s, n) : n;
        return (left_count < right_count) ? left_count : right_count;
    }

    int sync() override {
        int left_result = left_ ? left_->pubsync() : 0;
        int right_result = right_ ? right_->pubsync() : 0;
        return (left_result == 0 && right_result == 0) ? 0 : -1;
    }

private:
    std::streambuf* left_;
    std::streambuf* right_;
};

class TeeStream {
public:
    TeeStream(std::ostream& stream, std::streambuf* other)
        : stream_(stream),
          old_buf_(stream.rdbuf()),
          tee_buf_(old_buf_, other) {
        stream_.rdbuf(&tee_buf_);
    }

    ~TeeStream() {
        stream_.rdbuf(old_buf_);
    }

    TeeStream(const TeeStream&) = delete;
    TeeStream& operator=(const TeeStream&) = delete;

private:
    std::ostream& stream_;
    std::streambuf* old_buf_;
    TeeStreambuf tee_buf_;
};
