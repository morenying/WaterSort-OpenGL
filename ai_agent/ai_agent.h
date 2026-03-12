/*
 * ai_agent.h - AI Agent Core Module
 *
 * The main entry point of the AIAgent reusable library.
 * Combines LLM client, knowledge base (RAG), and conversation management
 * into a cohesive, game-agnostic AI assistant.
 *
 * Design principles:
 *   - High cohesion: each module (LLM, KB, Agent) is self-contained
 *   - Low coupling: no dependency on any specific game or UI framework
 *   - Reusable: drop ai_agent/ folder into any C++ project
 *   - Thread-safe: async query support via callbacks
 *
 * Integration pattern:
 *   1. Include this header
 *   2. Create an AIAgent instance
 *   3. Configure LLM and knowledge base
 *   4. Call askAsync() when user sends a message
 *   5. Poll or use callback to get the response
 *   6. Render the response in your game/app UI (your responsibility)
 *
 * Usage:
 *   #define AI_AGENT_IMPLEMENTATION  // In ONE .cpp file only
 *   #include "ai_agent/ai_agent.h"
 *
 *   AIAgent agent;
 *   agent.init("ai_config.txt", "knowledge.txt");
 *   agent.setSystemPrompt("You are a game assistant...");
 *   agent.askAsync("How do I win?", [](const std::string& reply) {
 *       // Display reply in your UI
 *   });
 */

#ifndef AI_AGENT_H
#define AI_AGENT_H

#include "json_utils.h"
#include "llm_client.h"
#include "knowledge_base.h"

#include <string>
#include <vector>
#include <functional>
#include <mutex>

namespace aiagent {

// Chat message for conversation history
struct ChatMessage {
    std::string role;       // "user", "assistant", "system"
    std::string content;
    float timestamp;        // For display animation
    int displayedChars;     // For typewriter effect (-1 = fully displayed)
    
    ChatMessage() : timestamp(0), displayedChars(-1) {}
    ChatMessage(const std::string& r, const std::string& c)
        : role(r), content(c), timestamp(0), displayedChars(-1) {}
};

// Agent state
enum class AgentState {
    Idle,       // Ready for input
    Querying,   // Waiting for LLM response
    Error       // Last query failed
};

// Callback types
using ResponseCallback = std::function<void(const std::string& response, bool success)>;
using StateChangeCallback = std::function<void(AgentState newState)>;

class AIAgent {
public:
    AIAgent();
    ~AIAgent();
    
    // ---- Initialization ----
    
    // Initialize with config file and optional knowledge file
    bool init(const std::string& configFile, const std::string& knowledgeFile = "");
    
    // Manual configuration
    void configureLLM(const std::string& endpoint, const std::string& apiKey,
                      const std::string& model);
    
    // Set the system prompt (who is the AI?)
    void setSystemPrompt(const std::string& prompt);
    
    // ---- Knowledge Base ----
    
    // Get mutable reference to knowledge base for adding chunks
    KnowledgeBase& getKnowledgeBase() { return kb_; }
    
    // Set dynamic context provider (e.g., current game state serializer)
    void setContextProvider(std::function<std::string()> provider);
    
    // ---- Conversation ----
    
    // Ask a question (blocking - returns response directly)
    std::string ask(const std::string& userMessage);
    
    // Ask a question (async - calls callback when done)
    void askAsync(const std::string& userMessage, ResponseCallback callback);
    
    // Get current state
    AgentState getState() const;
    
    // Get conversation history
    const std::vector<ChatMessage>& getHistory() const { return history_; }
    
    // Clear conversation history
    void clearHistory();
    
    // Get last error message
    std::string getLastError() const;
    
    // Set state change callback
    void onStateChange(StateChangeCallback callback);
    
    // ---- Typewriter Effect Support ----
    
    // Update displayed characters for the latest assistant message
    // Call this each frame. Returns true if animation is still in progress.
    bool updateTypewriter(float deltaTime, float charsPerSecond = 30.0f);
    
    // Get the currently visible text of the latest assistant message
    std::string getVisibleLastReply() const;
    
    // Skip typewriter animation (show full text immediately)
    void skipTypewriter();
    
    // ---- Configuration ----
    
    int maxHistoryMessages;    // Max messages to keep (default: 20)
    int ragTopK;               // Number of RAG chunks to retrieve (default: 3)
    
private:
    LLMClient llm_;
    KnowledgeBase kb_;
    std::string systemPrompt_;
    std::vector<ChatMessage> history_;
    AgentState state_;
    std::string lastError_;
    StateChangeCallback stateCallback_;
    mutable std::mutex mutex_;
    float typewriterAccum_;
    
    void setState(AgentState s);
    std::vector<JsonValue> buildMessages(const std::string& userMessage);
};

} // namespace aiagent

// ============================================================
// Implementation
// ============================================================
#ifdef AI_AGENT_IMPLEMENTATION
#ifndef AI_AGENT_IMPL_GUARD
#define AI_AGENT_IMPL_GUARD

#include <thread>
#include <ctime>

namespace aiagent {

AIAgent::AIAgent()
    : state_(AgentState::Idle)
    , maxHistoryMessages(20)
    , ragTopK(3)
    , typewriterAccum_(0)
{
    systemPrompt_ = "You are a helpful AI assistant.";
}

AIAgent::~AIAgent() {}

bool AIAgent::init(const std::string& configFile, const std::string& knowledgeFile) {
    bool ok = llm_.loadConfig(configFile);
    if (!ok) {
        lastError_ = "Failed to load config from: " + configFile;
        return false;
    }
    
    if (!knowledgeFile.empty()) {
        kb_.loadFromFile(knowledgeFile);
    }
    
    return true;
}

void AIAgent::configureLLM(const std::string& endpoint, const std::string& apiKey,
                             const std::string& model) {
    llm_.configure(endpoint, apiKey, model);
}

void AIAgent::setSystemPrompt(const std::string& prompt) {
    systemPrompt_ = prompt;
}

void AIAgent::setContextProvider(std::function<std::string()> provider) {
    kb_.setDynamicContext(provider);
}

AgentState AIAgent::getState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

void AIAgent::setState(AgentState s) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = s;
    }
    if (stateCallback_) stateCallback_(s);
}

std::string AIAgent::getLastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}

void AIAgent::clearHistory() {
    std::lock_guard<std::mutex> lock(mutex_);
    history_.clear();
}

void AIAgent::onStateChange(StateChangeCallback callback) {
    stateCallback_ = callback;
}

std::vector<JsonValue> AIAgent::buildMessages(const std::string& userMessage) {
    std::vector<JsonValue> messages;
    
    // 1. System prompt + RAG context
    std::string ragContext = kb_.retrieve(userMessage, ragTopK);
    std::string fullSystem = systemPrompt_;
    if (!ragContext.empty()) {
        fullSystem += "\n\n" + ragContext;
    }
    messages.push_back(jsonChatMessage("system", fullSystem));
    
    // 2. Conversation history (limited)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        int start = 0;
        if ((int)history_.size() > maxHistoryMessages) {
            start = (int)history_.size() - maxHistoryMessages;
        }
        for (int i = start; i < (int)history_.size(); i++) {
            messages.push_back(jsonChatMessage(history_[i].role, history_[i].content));
        }
    }
    
    // 3. Current user message
    messages.push_back(jsonChatMessage("user", userMessage));
    
    return messages;
}

std::string AIAgent::ask(const std::string& userMessage) {
    setState(AgentState::Querying);
    
    // Add user message to history
    {
        std::lock_guard<std::mutex> lock(mutex_);
        history_.push_back(ChatMessage("user", userMessage));
    }
    
    auto messages = buildMessages(userMessage);
    LLMResponse resp = llm_.chat(messages);
    
    if (resp.success) {
        std::lock_guard<std::mutex> lock(mutex_);
        ChatMessage assistantMsg("assistant", resp.content);
        assistantMsg.displayedChars = 0;  // Start typewriter
        typewriterAccum_ = 0;
        history_.push_back(assistantMsg);
        
        // Trim history
        while ((int)history_.size() > maxHistoryMessages * 2) {
            history_.erase(history_.begin());
        }
        
        state_ = AgentState::Idle;
        return resp.content;
    } else {
        std::lock_guard<std::mutex> lock(mutex_);
        lastError_ = resp.error;
        state_ = AgentState::Error;
        return "";
    }
}

void AIAgent::askAsync(const std::string& userMessage, ResponseCallback callback) {
    setState(AgentState::Querying);
    
    // Add user message to history
    {
        std::lock_guard<std::mutex> lock(mutex_);
        history_.push_back(ChatMessage("user", userMessage));
    }
    
    // Launch async thread
    std::thread([this, userMessage, callback]() {
        auto messages = buildMessages(userMessage);
        LLMResponse resp = llm_.chat(messages);
        
        if (resp.success) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                ChatMessage assistantMsg("assistant", resp.content);
                assistantMsg.displayedChars = 0;
                typewriterAccum_ = 0;
                history_.push_back(assistantMsg);
                
                while ((int)history_.size() > maxHistoryMessages * 2) {
                    history_.erase(history_.begin());
                }
            }
            setState(AgentState::Idle);
            if (callback) callback(resp.content, true);
        } else {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                lastError_ = resp.error;
            }
            setState(AgentState::Error);
            if (callback) callback(resp.error, false);
        }
    }).detach();
}

bool AIAgent::updateTypewriter(float deltaTime, float charsPerSecond) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (history_.empty()) return false;
    
    auto& last = history_.back();
    if (last.role != "assistant" || last.displayedChars < 0) return false;
    
    int totalChars = (int)last.content.size();
    if (last.displayedChars >= totalChars) {
        last.displayedChars = -1;  // Animation complete
        return false;
    }
    
    typewriterAccum_ += deltaTime * charsPerSecond;
    int newChars = (int)typewriterAccum_;
    if (newChars > 0) {
        last.displayedChars += newChars;
        typewriterAccum_ -= (float)newChars;
        
        // Handle multi-byte UTF-8: don't split in the middle
        const unsigned char* p = (const unsigned char*)last.content.c_str();
        int pos = last.displayedChars;
        if (pos < totalChars && (p[pos] & 0xC0) == 0x80) {
            // In the middle of a multi-byte char, advance to next char start
            while (pos < totalChars && (p[pos] & 0xC0) == 0x80) pos++;
            last.displayedChars = pos;
        }
        
        if (last.displayedChars >= totalChars) {
            last.displayedChars = -1;
            return false;
        }
    }
    
    return true;
}

std::string AIAgent::getVisibleLastReply() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (history_.empty()) return "";
    
    const auto& last = history_.back();
    if (last.role != "assistant") return "";
    
    if (last.displayedChars < 0) return last.content;
    return last.content.substr(0, last.displayedChars);
}

void AIAgent::skipTypewriter() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (history_.empty()) return;
    auto& last = history_.back();
    if (last.role == "assistant") last.displayedChars = -1;
}

} // namespace aiagent

#endif // AI_AGENT_IMPL_GUARD
#endif // AI_AGENT_IMPLEMENTATION
#endif // AI_AGENT_H
