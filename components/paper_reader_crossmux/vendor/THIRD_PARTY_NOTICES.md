# Notices retained with the reading engine

CrossMux fixed source: 7dcd8b19031c9277376c7e0f8cacda4d7b5a43a1.
Its MIT license is in LICENSE; original file headers are retained.

- Expat: upstream version header 2.7.3+. Copyright holders and MIT permission
  text remain in each vendored Expat source/header.
- MiniBidi: source credits Ahmad Khalifa (Arabeyes, MIT) and Thomas Wolff
  (Mintty changes); original attribution is retained in minibidi.c.
- English hyphenation trie: imported unchanged from CrossMux's generated
  hyph-en.trie.h. CrossMux's scripts/update_hyphenation.sh obtains tries from
  https://github.com/typst/hypher . No other language trie is included here.
  Hypher code is MIT/Apache-2.0; language patterns have separate notices.
  American English pattern notice from
  https://github.com/typst/hypher/blob/main/patterns/hyph-en-us.tex :

  Copyright (C) 1990, 2004, 2005 Gerard D.C. Kuiken

  Copying and distribution of this file, with or without modification,
  are permitted in any medium without royalty provided the copyright
  notice and this notice are preserved.

  The generated trie is pinned to CrossMux, not regenerated from today's
  Hypher main branch. This notice records its upstream provenance and does
  not assert that today's pattern/trie bytes equal the pinned trie.

Local changes are listed in the component README and the generated
CROSSMUX_VENDOR_PERF10.json audit. No upstream OTA, font-slot reuse,
hardware initialization, cloud service or alternative shell is included.
