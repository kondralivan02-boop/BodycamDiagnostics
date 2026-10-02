#include <cstdio>

int main() {
    std::fputs(
        "ffmpeg fixture\n"
        "  Duration: 00:00:10.00, start: 0.000000, bitrate: 1 kb/s\n",
        stderr);
    return 1;
}
