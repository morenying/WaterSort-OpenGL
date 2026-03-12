/*
 * knowledge_base.h - RAG Knowledge Base with BM25 Retrieval
 *
 * Part of the AIAgent reusable module.
 * Implements a lightweight Retrieval-Augmented Generation knowledge base.
 *
 * How it works:
 *   1. Knowledge is stored as "chunks" — short text passages with keywords
 *   2. When a user asks a question, the query is tokenized
 *   3. BM25 scoring ranks chunks by relevance
 *   4. Top-K relevant chunks are injected into the LLM prompt
 *
 * This approach gives the LLM domain-specific context without fine-tuning.
 *
 * Usage:
 *   KnowledgeBase kb;
 *   kb.addChunk("Game Rules", "water sort puzzle rules...", {"rules", "how to play"});
 *   kb.addChunk("Strategy", "tips and tricks...", {"strategy", "tips"});
 *   std::string context = kb.retrieve("how do I play?", 3);
 */

#ifndef KNOWLEDGE_BASE_H
#define KNOWLEDGE_BASE_H

#include <string>
#include <vector>
#include <map>
#include <set>

namespace aiagent {

struct KnowledgeChunk {
    std::string title;      // Chunk title/category
    std::string content;    // The actual knowledge text
    std::vector<std::string> keywords;  // Manual keywords for boosting
    std::vector<std::string> tokens;    // Auto-tokenized words (internal)
};

struct RetrievalResult {
    int chunkIndex;
    float score;
    const KnowledgeChunk* chunk;
};

class KnowledgeBase {
public:
    KnowledgeBase();
    
    // Add a knowledge chunk
    void addChunk(const std::string& title, const std::string& content,
                  const std::vector<std::string>& keywords = {});
    
    // Load knowledge from a text file (format: ## Title \n content \n keywords: a,b,c)
    bool loadFromFile(const std::string& filePath);
    
    // Retrieve top-K relevant chunks for a query
    // Returns formatted context string ready for LLM prompt injection
    std::string retrieve(const std::string& query, int topK = 3) const;
    
    // Get raw retrieval results with scores
    std::vector<RetrievalResult> search(const std::string& query, int topK = 3) const;
    
    // Get total number of chunks
    int size() const { return (int)chunks_.size(); }
    
    // Clear all chunks
    void clear() { chunks_.clear(); idf_.clear(); avgDocLen_ = 0; }
    
    // Add a dynamic context provider (e.g., current game state)
    // This function is called every time retrieve() runs and its output
    // is prepended to the context.
    void setDynamicContext(std::function<std::string()> provider) {
        dynamicContextProvider_ = provider;
    }

private:
    std::vector<KnowledgeChunk> chunks_;
    std::map<std::string, float> idf_;  // Inverse document frequency
    float avgDocLen_;
    std::function<std::string()> dynamicContextProvider_;
    
    // Tokenize text into words (handles Chinese + English)
    std::vector<std::string> tokenize(const std::string& text) const;
    
    // Rebuild IDF index
    void rebuildIndex();
    
    // BM25 score for a single document against a query
    float bm25Score(const KnowledgeChunk& chunk, 
                    const std::vector<std::string>& queryTokens) const;
};

} // namespace aiagent

// ============================================================
// Implementation
// ============================================================
#ifdef AI_AGENT_IMPLEMENTATION
#ifndef KNOWLEDGE_BASE_IMPL_GUARD
#define KNOWLEDGE_BASE_IMPL_GUARD

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace aiagent {

KnowledgeBase::KnowledgeBase() : avgDocLen_(0) {}

std::vector<std::string> KnowledgeBase::tokenize(const std::string& text) const {
    std::vector<std::string> tokens;
    std::string current;
    
    const unsigned char* p = (const unsigned char*)text.c_str();
    size_t len = text.size();
    size_t i = 0;
    
    while (i < len) {
        unsigned char c = p[i];
        
        if (c >= 0x80) {
            // Multi-byte UTF-8 character (Chinese, etc.)
            // Flush any accumulated ASCII word
            if (!current.empty()) {
                // Convert to lowercase
                for (auto& ch : current) ch = (char)tolower((unsigned char)ch);
                tokens.push_back(current);
                current.clear();
            }
            
            // Determine UTF-8 byte count
            int bytes = 1;
            if ((c & 0xE0) == 0xC0) bytes = 2;
            else if ((c & 0xF0) == 0xE0) bytes = 3;
            else if ((c & 0xF8) == 0xF0) bytes = 4;
            
            // Each CJK character is its own token (character-level tokenization)
            if (i + bytes <= len) {
                tokens.push_back(std::string((const char*)p + i, bytes));
                
                // Also create bigrams for better Chinese matching
                if (i + bytes < len && p[i + bytes] >= 0x80) {
                    int nextBytes = 1;
                    if ((p[i + bytes] & 0xE0) == 0xC0) nextBytes = 2;
                    else if ((p[i + bytes] & 0xF0) == 0xE0) nextBytes = 3;
                    else if ((p[i + bytes] & 0xF8) == 0xF0) nextBytes = 4;
                    
                    if (i + bytes + nextBytes <= len) {
                        tokens.push_back(std::string((const char*)p + i, bytes + nextBytes));
                    }
                }
            }
            i += bytes;
        } else if (isalnum(c)) {
            current += (char)c;
            i++;
        } else {
            // Whitespace or punctuation: flush word
            if (!current.empty()) {
                for (auto& ch : current) ch = (char)tolower((unsigned char)ch);
                tokens.push_back(current);
                current.clear();
            }
            i++;
        }
    }
    
    if (!current.empty()) {
        for (auto& ch : current) ch = (char)tolower((unsigned char)ch);
        tokens.push_back(current);
    }
    
    return tokens;
}

void KnowledgeBase::rebuildIndex() {
    idf_.clear();
    if (chunks_.empty()) { avgDocLen_ = 0; return; }
    
    int N = (int)chunks_.size();
    std::map<std::string, int> docFreq;  // How many docs contain each term
    float totalLen = 0;
    
    for (auto& chunk : chunks_) {
        chunk.tokens = tokenize(chunk.content + " " + chunk.title);
        // Add keyword tokens with boost
        for (const auto& kw : chunk.keywords) {
            auto kwTokens = tokenize(kw);
            for (const auto& t : kwTokens) {
                // Add keyword tokens multiple times for boosting
                chunk.tokens.push_back(t);
                chunk.tokens.push_back(t);
            }
        }
        
        totalLen += (float)chunk.tokens.size();
        
        // Count unique terms in this document
        std::set<std::string> seen;
        for (const auto& t : chunk.tokens) seen.insert(t);
        for (const auto& t : seen) docFreq[t]++;
    }
    
    avgDocLen_ = totalLen / N;
    
    // Calculate IDF: log((N - df + 0.5) / (df + 0.5) + 1)
    for (auto& kv : docFreq) {
        float df = (float)kv.second;
        idf_[kv.first] = logf((N - df + 0.5f) / (df + 0.5f) + 1.0f);
    }
}

float KnowledgeBase::bm25Score(const KnowledgeChunk& chunk,
                                const std::vector<std::string>& queryTokens) const {
    const float k1 = 1.2f;
    const float b = 0.75f;
    
    float docLen = (float)chunk.tokens.size();
    float score = 0;
    
    // Count term frequencies in this document
    std::map<std::string, int> tf;
    for (const auto& t : chunk.tokens) tf[t]++;
    
    for (const auto& qt : queryTokens) {
        auto idfIt = idf_.find(qt);
        if (idfIt == idf_.end()) continue;
        
        float idfScore = idfIt->second;
        float termFreq = 0;
        auto tfIt = tf.find(qt);
        if (tfIt != tf.end()) termFreq = (float)tfIt->second;
        
        // BM25 formula
        float numerator = termFreq * (k1 + 1.0f);
        float denominator = termFreq + k1 * (1.0f - b + b * docLen / avgDocLen_);
        score += idfScore * numerator / denominator;
    }
    
    return score;
}

void KnowledgeBase::addChunk(const std::string& title, const std::string& content,
                              const std::vector<std::string>& keywords) {
    KnowledgeChunk chunk;
    chunk.title = title;
    chunk.content = content;
    chunk.keywords = keywords;
    chunks_.push_back(chunk);
    rebuildIndex();
}

bool KnowledgeBase::loadFromFile(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) return false;
    
    std::string line;
    std::string currentTitle;
    std::string currentContent;
    std::vector<std::string> currentKeywords;
    
    auto flushChunk = [&]() {
        if (!currentTitle.empty() && !currentContent.empty()) {
            addChunk(currentTitle, currentContent, currentKeywords);
        }
        currentTitle.clear();
        currentContent.clear();
        currentKeywords.clear();
    };
    
    while (std::getline(file, line)) {
        // Remove trailing \r
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        
        if (line.substr(0, 3) == "## ") {
            flushChunk();
            currentTitle = line.substr(3);
        } else if (line.substr(0, 10) == "keywords: ") {
            // Parse comma-separated keywords
            std::string kwStr = line.substr(10);
            std::istringstream iss(kwStr);
            std::string kw;
            while (std::getline(iss, kw, ',')) {
                // Trim
                while (!kw.empty() && kw[0] == ' ') kw.erase(kw.begin());
                while (!kw.empty() && kw.back() == ' ') kw.pop_back();
                if (!kw.empty()) currentKeywords.push_back(kw);
            }
        } else {
            if (!line.empty()) {
                if (!currentContent.empty()) currentContent += "\n";
                currentContent += line;
            }
        }
    }
    
    flushChunk();
    return !chunks_.empty();
}

std::vector<RetrievalResult> KnowledgeBase::search(const std::string& query, int topK) const {
    std::vector<std::string> queryTokens = tokenize(query);
    
    std::vector<RetrievalResult> results;
    for (int i = 0; i < (int)chunks_.size(); i++) {
        RetrievalResult r;
        r.chunkIndex = i;
        r.chunk = &chunks_[i];
        r.score = bm25Score(chunks_[i], queryTokens);
        if (r.score > 0) results.push_back(r);
    }
    
    // Sort by score descending
    std::sort(results.begin(), results.end(),
              [](const RetrievalResult& a, const RetrievalResult& b) {
                  return a.score > b.score;
              });
    
    if ((int)results.size() > topK) results.resize(topK);
    return results;
}

std::string KnowledgeBase::retrieve(const std::string& query, int topK) const {
    std::string context;
    
    // Add dynamic context if available
    if (dynamicContextProvider_) {
        std::string dynamic = dynamicContextProvider_();
        if (!dynamic.empty()) {
            context += dynamic + "\n\n";
        }
    }
    
    auto results = search(query, topK);
    
    if (!results.empty()) {
        context += "--- Related Knowledge ---\n";
        for (const auto& r : results) {
            context += "[" + r.chunk->title + "]\n";
            context += r.chunk->content + "\n\n";
        }
    }
    
    return context;
}

} // namespace aiagent

#endif // KNOWLEDGE_BASE_IMPL_GUARD
#endif // AI_AGENT_IMPLEMENTATION
#endif // KNOWLEDGE_BASE_H
