# https://github.com/takeiteasy/pal
# 
# Copyright 2025 George Watson
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
 
from setuptools import setup, Extension
import platform
import subprocess

with open("README.md", "r", encoding="utf-8") as fh:
    long_description = fh.read()

setup(
    name="pal",
    version="0.0.1",
    author="George Watson",
    author_email="gigolo@hotmail.co.uk",
    description="Python Audio Library (raudio bindings)",
    long_description=long_description,
    long_description_content_type="text/markdown",
    url="https://github.com/takeiteasy/pal",
    ext_modules=[Extension("pal",
                           include_dirs=["raudio"],
                           define_macros=[("RAUDIO_STANDALONE", 1),
                                          ("SUPPORT_MODULE_RAUDIO", 1),
                                          ("SUPPORT_FILEFORMAT_WAV", 1),
                                          ("SUPPORT_FILEFORMAT_OGG", 1),
                                          ("SUPPORT_FILEFORMAT_MP3", 1),
                                          ("SUPPORT_FILEFORMAT_QOA", 1),
                                          ("SUPPORT_FILEFORMAT_FLAC", 1),
                                          ("SUPPORT_FILEFORMAT_XM", 1),
                                          ("SUPPORT_FILEFORMAT_MOD", 1)],
                           sources=["pal.c", "raudio/raudio.c"])],
    classifiers=[
        "Programming Language :: Python :: 3",
        "License :: OSI Approved :: MIT License",
        "Operating System :: OS Independent",
    ]
)

