"""Compile the reader adapter against a temporary SD root; never touch a device."""
import argparse
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument("--idf",type=Path,required=True)
parser.add_argument("--epub",type=Path)
args=parser.parse_args()
cjson=args.idf.resolve()/"components/json/cJSON"
with tempfile.TemporaryDirectory(prefix="metalio-reader-test-") as directory:
    temp=Path(directory)
    sd=temp/"sd"
    sd.mkdir()
    subprocess.run(["clang","-fsanitize=address,undefined","-c",str(cjson/"cJSON.c"),"-o",str(temp/"cjson.o")],check=True)
    subprocess.run(["clang++","-std=c++17","-Wall","-Wextra","-Werror","-Wno-unused-function",
                    "-fsanitize=address,undefined","-fno-omit-frame-pointer",
                    "-DENABLE_CHINESE_VERSION=1",f'-DINKDESK_SD_ROOT="{sd}"',
                    "-I"+str(root/"tests/native_stubs"),"-I"+str(root/"main"),"-I"+str(cjson),
                    str(root/"main/inkdesk_reader.cc"),str(root/"main/crossmux_txt/TxtEncoding.cpp"),
                    str(root/"main/inkdesk_epub.cc"),str(root/"main/reader/epub_document.cc"),
                    str(root/"main/reader/zip_reader.cc"),str(root/"main/reader/html_content.cc"),
                    str(root/"tests/native_image_stub.cpp"),"-lz",
                    str(root/"tests/inkdesk_reader_test.cpp"),str(temp/"cjson.o"),"-o",str(temp/"test")],check=True)
    subprocess.run([str(temp/"test")]+([str(args.epub.resolve())] if args.epub else []),check=True)
