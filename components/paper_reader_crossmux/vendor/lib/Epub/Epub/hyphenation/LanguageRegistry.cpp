#include "LanguageRegistry.h"
#include "HyphenationCommon.h"
#include "generated/hyph-en.trie.h"
namespace {LanguageHyphenator english(en_patterns,isLatinLetter,toLowerLatin,3,3);
const LanguageEntry entries[]={{"english","en",&english}};}
const LanguageHyphenator* getLanguageHyphenatorForPrimaryTag(const std::string& tag){return tag=="en"?&english:nullptr;}
LanguageEntryView getLanguageEntries(){return {entries,1};}
