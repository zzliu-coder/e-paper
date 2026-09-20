#pragma once
#include "text.hpp"
namespace paper {
    struct Candidate {
        std::string text,pinyin;
        uint32_t score=0;
    };
    class PinyinDictionary {
        Store&store_;
        bool allowTest_=false;
        std::map<std::string,uint32_t>learned_;
        public: explicit PinyinDictionary(Store&s,bool allowTest=false):store_(s),allowTest_(allowTest) {
        }
        Status query(const std::string&input,bool nine,std::vector<Candidate>&out,size_t limit=64);
        Status learn(const std::vector<Candidate>&);
        Status loadLearning();
        static std::string nineKey(const std::string&);
        static Status normalize(std::string&);
    };
    enum class InputMode {
        Pinyin9,Pinyin26,English,Numbers,Symbols
    };
    class TextSession {
        PinyinDictionary&dict_;
        std::string original_,draft_,preedit_;
        size_t cursor_=0,maxBytes_=256;
        bool secret_=false,active_=false;
        InputMode mode_=InputMode::Pinyin9;
        std::vector<Candidate>choices_,learnPending_;
        uint64_t generation_=0;
        void wipe(std::string&);
        Status refresh();
        public: explicit TextSession(PinyinDictionary&d):dict_(d) {
        }
        ~TextSession();
        Status begin(const std::string&initial,size_t maxBytes,bool secret=false);
        Status key(char);
        Status literal(const std::string&);
        Status backspace();
        Status move(int direction);
        Status mode(InputMode);
        Status choose(size_t index,uint64_t generation);
        Status confirm(std::string&out);
        void cancel();
        bool active()const {
            return active_;
        }
        bool secret()const {
            return secret_;
        }
        InputMode mode()const {
            return mode_;
        }
        uint64_t generation()const {
            return generation_;
        }
        size_t cursor()const {
            return cursor_;
        }
        const std::vector<Candidate>&candidates()const {
            return choices_;
        }
        const std::string&preedit()const {
            return preedit_;
        }
        std::string visible()const;
    };
}
