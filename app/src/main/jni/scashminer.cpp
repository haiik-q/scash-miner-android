#include <jni.h>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <android/log.h>

#define LOG_TAG "ScashMiner"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Global mining state
static std::atomic<bool> g_mining(false);
static std::atomic<double> g_hashrate(0.0);
static std::atomic<bool> g_pool_connected(false);
static std::string g_status = "Idle";
static std::vector<std::thread> g_mining_threads;
static std::thread g_dev_thread;

// Mining parameters
static std::string g_pool_url;
static std::string g_wallet_address;
static std::string g_dev_wallet;
static float g_dev_fee = 0.05f;
static int g_num_threads = 1;
static int g_pool_socket = -1;

// Stratum protocol implementation
class StratumClient {
private:
    std::string host;
    int port;
    int sock;
    bool connected;

public:
    StratumClient() : sock(-1), connected(false) {}

    bool connect(const std::string& poolUrl) {
        // Parse pool URL (format: pool.domain.com:port)
        size_t colonPos = poolUrl.find(':');
        if (colonPos == std::string::npos) {
            LOGE("Invalid pool URL format");
            return false;
        }

        host = poolUrl.substr(0, colonPos);
        port = std::stoi(poolUrl.substr(colonPos + 1));

        LOGI("Connecting to pool: %s:%d", host.c_str(), port);

        // Create socket
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            LOGE("Failed to create socket");
            return false;
        }

        // Set socket timeout
        struct timeval timeout;
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

        // Resolve hostname
        struct hostent* server = gethostbyname(host.c_str());
        if (server == nullptr) {
            LOGE("Failed to resolve hostname: %s", host.c_str());
            close(sock);
            sock = -1;
            return false;
        }

        // Connect to server
        struct sockaddr_in serv_addr;
        memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
        serv_addr.sin_port = htons(port);

        if (::connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            LOGE("Failed to connect to pool");
            close(sock);
            sock = -1;
            return false;
        }

        connected = true;
        g_pool_connected = true;
        LOGI("Successfully connected to pool");
        return true;
    }

    bool subscribe() {
        if (!connected) return false;

        // Send subscribe message (Stratum protocol)
        std::string subscribe_msg = "{\"id\":1,\"method\":\"mining.subscribe\",\"params\":[\"ScashMiner/1.0\"]}\n";
        return sendMessage(subscribe_msg);
    }

    bool authorize(const std::string& wallet) {
        if (!connected) return false;

        // Send authorize message
        std::string auth_msg = "{\"id\":2,\"method\":\"mining.authorize\",\"params\":[\"" + wallet + "\",\"x\"]}\n";
        return sendMessage(auth_msg);
    }

    bool sendMessage(const std::string& msg) {
        if (!connected || sock < 0) return false;

        ssize_t sent = send(sock, msg.c_str(), msg.length(), 0);
        if (sent < 0) {
            LOGE("Failed to send message");
            disconnect();
            return false;
        }
        return true;
    }

    std::string receiveMessage() {
        if (!connected || sock < 0) return "";

        char buffer[4096];
        ssize_t received = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (received <= 0) {
            if (received < 0) {
                LOGE("Failed to receive message");
            }
            disconnect();
            return "";
        }

        buffer[received] = '\0';
        return std::string(buffer);
    }

    void disconnect() {
        if (sock >= 0) {
            close(sock);
            sock = -1;
        }
        connected = false;
        g_pool_connected = false;
    }

    bool isConnected() const {
        return connected;
    }
};

static StratumClient g_stratum_client;
static StratumClient g_dev_stratum_client;

// Mining worker function
void miningWorker(const std::string& poolUrl, const std::string& wallet, bool isDevFee) {
    StratumClient& client = isDevFee ? g_dev_stratum_client : g_stratum_client;

    // Connect to pool
    if (!client.connect(poolUrl)) {
        LOGE("Failed to connect to pool: %s", poolUrl.c_str());
        if (!isDevFee) {
            g_status = "Connection failed";
        }
        return;
    }

    // Subscribe
    if (!client.subscribe()) {
        LOGE("Failed to subscribe");
        client.disconnect();
        return;
    }

    // Wait for subscribe response
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::string response = client.receiveMessage();
    LOGI("Subscribe response: %s", response.c_str());

    // Authorize
    if (!client.authorize(wallet)) {
        LOGE("Failed to authorize");
        client.disconnect();
        return;
    }

    // Wait for authorize response
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    response = client.receiveMessage();
    LOGI("Authorize response: %s", response.c_str());

    if (!isDevFee) {
        g_status = "Mining";
    }

    // Simulate mining with hashrate calculation
    auto startTime = std::chrono::steady_clock::now();
    uint64_t hashes = 0;
    const uint64_t HASHES_PER_ITERATION = 1000;

    while (g_mining && client.isConnected()) {
        // Simulate hashing work
        // In a real implementation, this would call RandomX hashing
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        hashes += HASHES_PER_ITERATION;

        // Calculate hashrate every second
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();

        if (elapsed > 0 && !isDevFee) {
            double hashrate = static_cast<double>(hashes) / elapsed;
            g_hashrate = hashrate;
        }

        // Check for pool messages
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(client.sock, &readfds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 0;

        int activity = select(client.sock + 1, &readfds, NULL, NULL, &tv);
        if (activity > 0 && FD_ISSET(client.sock, &readfds)) {
            std::string msg = client.receiveMessage();
            if (!msg.empty()) {
                LOGI("Pool message: %s", msg.c_str());
            }
        }
    }

    client.disconnect();
}

// JNI functions
extern "C" JNIEXPORT jint JNICALL
Java_com_scash_miner_MainActivity_startMining(
        JNIEnv* env,
        jobject /* this */,
        jstring poolUrl,
        jstring walletAddress,
        jstring devWallet,
        jfloat devFee,
        jint threads) {

    if (g_mining) {
        return -1; // Already mining
    }

    const char* poolStr = env->GetStringUTFChars(poolUrl, nullptr);
    const char* walletStr = env->GetStringUTFChars(walletAddress, nullptr);
    const char* devWalletStr = env->GetStringUTFChars(devWallet, nullptr);

    g_pool_url = poolStr;
    g_wallet_address = walletStr;
    g_dev_wallet = devWalletStr;
    g_dev_fee = devFee;
    g_num_threads = threads;

    env->ReleaseStringUTFChars(poolUrl, poolStr);
    env->ReleaseStringUTFChars(walletAddress, walletStr);
    env->ReleaseStringUTFChars(devWallet, devWalletStr);

    g_mining = true;
    g_status = "Starting";
    g_hashrate = 0.0;

    LOGI("Starting mining: pool=%s, wallet=%s, threads=%d, devFee=%.2f%%",
         g_pool_url.c_str(), g_wallet_address.c_str(), g_num_threads, g_dev_fee * 100);

    // Start multiple mining threads
    g_mining_threads.clear();
    for (int i = 0; i < g_num_threads; i++) {
        g_mining_threads.push_back(std::thread([=]() {
            LOGI("Mining thread %d started", i);
            miningWorker(g_pool_url, g_wallet_address, false);
            LOGI("Mining thread %d stopped", i);
        }));
    }

    // Start dev fee mining thread (runs 5% of the time)
    g_dev_thread = std::thread([=]() {
        while (g_mining) {
            // Mine for dev wallet 5% of the time (3 seconds every minute)
            std::this_thread::sleep_for(std::chrono::seconds(57));

            if (!g_mining) break;

            LOGI("Starting dev fee mining");
            miningWorker(g_pool_url, g_dev_wallet, true);

            std::this_thread::sleep_for(std::chrono::seconds(3));
        }
    });

    return 0;
}

extern "C" JNIEXPORT void JNICALL
Java_com_scash_miner_MainActivity_stopMining(JNIEnv* env, jobject /* this */) {
    if (!g_mining) return;

    LOGI("Stopping mining");
    g_mining = false;
    g_status = "Stopped";

    g_stratum_client.disconnect();
    g_dev_stratum_client.disconnect();

    // Join all mining threads
    for (auto& thread : g_mining_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    g_mining_threads.clear();

    // Join dev fee thread
    if (g_dev_thread.joinable()) {
        g_dev_thread.join();
    }

    g_hashrate = 0.0;
    g_pool_connected = false;
}

extern "C" JNIEXPORT jdouble JNICALL
Java_com_scash_miner_MainActivity_getHashrate(JNIEnv* env, jobject /* this */) {
    return g_hashrate.load();
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_scash_miner_MainActivity_getMiningStatus(JNIEnv* env, jobject /* this */) {
    return env->NewStringUTF(g_status.c_str());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_scash_miner_MainActivity_isPoolConnected(JNIEnv* env, jobject /* this */) {
    return g_pool_connected.load();
}

extern "C" JNIEXPORT jint JNICALL
Java_com_scash_miner_MainActivity_getCpuCores(JNIEnv* env, jobject /* this */) {
    // Get number of CPU cores
    unsigned int cores = std::thread::hardware_concurrency();
    LOGI("Detected CPU cores: %u", cores);
    return static_cast<jint>(cores > 0 ? cores : 1);
}
