#include "MatchingEngine.hpp"
#include "util/StringUtil.hpp"
#include "domain/commandParser.hpp"
#include "util/ThreadSafeQueue.hpp"

#include <atomic>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <sstream>

// POSIX networking (C APIs used from C++).
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

/**
 * Global queues used for inter-thread communication.
 *
 * Rationale:
 *   The engine uses a strict 3‑thread pipeline:
 *
 *       [UDP Input] → [Processing] → [Output]
 *
 *   Each stage communicates through a ThreadSafeQueue<T>, ensuring:
 *     - Deterministic FIFO ordering
 *     - No shared mutable state between threads
 *     - No locking inside the matching engine itself
 *
 *   This architecture isolates concerns:
 *     - Input thread handles networking only
 *     - Processing thread handles all matching logic
 *     - Output thread serializes stdout
 *
 *   This design guarantees deterministic behavior even under concurrency.
 */
ThreadSafeQueue<Command> g_inputQueue;
ThreadSafeQueue<std::string> g_outputQueue;

/**
 * Global shutdown flag: set to true by signal handler to request clean termination.
 *
 * Used by:
 *   - Input thread (to exit recvfrom loop)
 *   - Signal handler (SIGTERM)
 *
 * The processing and output threads dont use it and exit automatically when their queues
 * are shutdown(), ensuring graceful termination.
 */
std::atomic<bool> g_shutdownRequested{false};

/**
 * Input Thread
 * --------------------------------------------------------------------------
 * Binds UDP port 1234 and receives CSV commands.
 *
 * Responsibilities:
 *   - Own the UDP socket exclusively
 *   - Receive datagrams (each may contain multiple lines)
 *   - Split into newline-separated commands
 *   - Parse each into a Command structure
 *   - Push commands into g_inputQueue
 *
 * Architectural Notes:
 *   - UDP is handled in its own thread to avoid blocking the matching engine.
 *   - recvfrom() preserves packet boundaries, which matches the test harness.
 *     - recvfrom() is a datagram‑oriented receive call.
 *     - When we use it on a UDP socket, each call returns exactly one UDP datagram — no more, no less.
         In other words:
                      If the sender sends one packet, we receive one packet.
                      If the sender sends two packets, we must call recvfrom() twice to get both.
 *   - No backpressure mechanism in UDP as UDP is lossy by design.
 *     We don’t need backpressure with UDP because the protocol already handles 
 *     overload by discarding data instead of slowing the sender.
 *   - The input thread never touches orderbook state directly.
 *
 * Determinism:
 *   - All commands are timestamped implicitly by queue order.
 *   - The processing thread consumes them strictly FIFO.
 */
void inputThreadFunc() {
    constexpr int port = 1234;

    // Create UDP socket.
    const int sockfd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        std::perror("socket() failed");
        g_shutdownRequested = true;
        g_inputQueue.shutdown();
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    //Bind this server to all the IP addresses(INADDR_ANY) that belong to this machine on port 1234.
    addr.sin_addr.s_addr = htonl(INADDR_ANY); 
    addr.sin_port = htons(port);

    // Allow quick rebinding after previous runs.
    //SO_REUSEADDR allows immediate rebinding to the same port even if previous connections are in TIME_WAIT state (crash or restart).
    const int opt = 1; //Enable above SO_REUSEADDR behavior
    ::setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /**
     * Retry bind() a few times.
     * Motivation:
     *   - When the engine restarts quickly, the OS may still hold port 1234
     *     in TIME_WAIT or a transient state.
     *   - Without retry logic, the engine may exit immediately on first run
     *     but succeed on second run.
     *
     * Behavior:
     *   - Try bind() up to 5 times
     *   - Sleep 100ms between attempts
     *   - If still failing, shut down cleanly
     */
    int attempts = 0;
    bool bound = false;
    while (attempts < 5) 
    {
        if (::bind(sockfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) 
        {
            bound = true;
            break;
        }

        std::perror("bind() failed, retrying");
        ++attempts;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!bound) {
        std::fprintf(stderr, "bind() failed after 5 attempts, giving up\n");
        ::close(sockfd);
        g_shutdownRequested = true;
        g_inputQueue.shutdown();
        return;
    }

    //4096 bytes = 4 KB buffer size for receiving UDP packets.
    //4 KB is the standard memory page(smallest chunk of memory OS manages at once) size on almost all modern OS
    constexpr std::size_t bufferSize = 4096;
    char buffer[bufferSize];

    while (!g_shutdownRequested.load()) 
    {
        sockaddr_in src{};
        socklen_t srcLen = sizeof(src);

        /**
         * recvfrom() Notes:
         *   - UDP delivers whole datagrams; partial reads do not occur.
         *   - EINTR/EAGAIN are normal and should be ignored.
         *   - n <= 0 means transient failure; continue receiving.
         * 
         * //UDP:: recvfrom() returns exactly one datagram per call (message boundaries) - no more, no less - 
         * //TCP: recv() : If we use SOCK_STREAM → TCP (byte stream, no message boundaries)
         */
        const ssize_t n = ::recvfrom(
            sockfd,
            buffer,
            bufferSize,
            0,
            reinterpret_cast<sockaddr*>(&src),
            &srcLen
        );

        if (n <= 0) {
            continue;
        }

        std::string data(buffer, buffer + n);
        std::istringstream iss(data);
        std::string line;

        while (std::getline(iss, line)) 
        {
            trim(line);
            if (line.empty()) {
                continue;
            }

            Command cmd = parseLineToCommand(line);
            if (cmd.type != CommandType::Invalid) {
                g_inputQueue.push(std::move(cmd));
            }
        }
    }

    ::close(sockfd);
    g_inputQueue.shutdown();
}

/**
 * Processing Thread
 * --------------------------------------------------------------------------
 * Consumes parsed commands and updates engine state.
 *
 * Responsibilities:
 *   - Own the MatchingEngine instance exclusively
 *   - Apply commands in strict FIFO order
 *   - Generate output lines (ack, trades, B-messages)
 *   - Push output lines into g_outputQueue
 *
 * Architectural Notes:
 *   - Only this thread mutates orderbook state.
 *   - No locks are required inside SymbolOrderbook or MatchingEngine.
 *   - This guarantees deterministic matching behavior.
 */
void processingThreadFunc() {
    MatchingEngine engine;
    Command cmd;

    while (g_inputQueue.pop(cmd)) {
        std::vector<std::string> outputs;
        engine.processCommand(cmd, outputs);

        for (auto& line : outputs) {
            g_outputQueue.push(std::move(line));
        }
    }

    g_outputQueue.shutdown();
}

/**
 * Output Thread
 * --------------------------------------------------------------------------
 * Serializes all output to stdout.
 *
 * Responsibilities:
 *   - Consume lines from g_outputQueue
 *   - Write each line to stdout
 *   - Flush after each line (required by test harness)
 *
 * Determinism:
 *   - Ensures no interleaving of output from multiple threads.
 *   - Guarantees strict FIFO output ordering.
 */
void outputThreadFunc() {
    std::string line;
    while (g_outputQueue.pop(line)) {
        std::cout << line << '\n';
        std::cout.flush();
    }
}

/**
 * Signal handler for SIGTERM.
 *
 * Behavior:
 *   - Sets g_shutdownRequested = true
 *   - Causes input thread to exit recvfrom loop
 *   - Other threads exit naturally when queues shut down
 */
void handleSigterm(int) {
    g_shutdownRequested = true;
}

}  // namespace

/**
 * main()
 * --------------------------------------------------------------------------
 * Entry point: starts input, processing, and output threads and waits for them.
 *
 * Architectural Guarantees:
 *   - Threads are started in a fixed order.
 *   - If inputThread fails (socket/bind), it shuts down queues.
 *   - Processing/output threads exit automatically when queues close.
 *   - main() joins all threads, preventing premature exit.
 *
 * Determinism:
 *   - All commands flow through a single processing thread.
 *   - Output is serialized through a dedicated output thread.
 */
int main() 
{
    // Register signal handler for graceful shutdown when receiving SIGTERM(POXIX signal number 15).
    std::signal(SIGTERM, handleSigterm);

    //Disable synchronization between C(scanf/printf) and C++ standard streams(cin/cout) for performance.
    std::ios::sync_with_stdio(false); 
    std::cin.tie(nullptr); //Untie cin from cout i.e. cin will not flush cout before input operation.

    std::thread inputThread(inputThreadFunc);
    std::thread processingThread(processingThreadFunc);
    std::thread outputThread(outputThreadFunc);

    inputThread.join();
    processingThread.join();
    outputThread.join();

    return 0;
}
