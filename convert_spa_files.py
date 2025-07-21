#!/usr/bin/env python3
"""
SPA 빌드 파일들을 C 코드로 변환하는 스크립트 (Gzip 압축 지원)
사용법: python convert_spa_files.py <spa_build_folder> <output_file>
"""

import os
import sys
import gzip
import argparse
from pathlib import Path

def file_to_c_array(file_path, var_name, use_gzip=True):
    """파일을 C 배열 형태로 변환 (Gzip 압축 옵션)"""
    with open(file_path, 'rb') as f:
        original_data = f.read()
    
    # Gzip 압축 여부 결정
    if use_gzip:
        compressed_data = gzip.compress(original_data, compresslevel=9)
        # 압축된 크기가 원본보다 큰 경우 압축하지 않음
        if len(compressed_data) < len(original_data):
            data = compressed_data
            is_compressed = True
            compression_ratio = len(original_data) / len(compressed_data)
        else:
            data = original_data
            is_compressed = False
            compression_ratio = 1.0
    else:
        data = original_data
        is_compressed = False
        compression_ratio = 1.0
    
    # C 배열 생성
    hex_data = ', '.join(f'0x{b:02X}' for b in data)
    
    # 80자 단위로 줄바꿈
    lines = []
    hex_data_list = hex_data.split(', ')
    for i in range(0, len(hex_data_list), 12):
        line = ', '.join(hex_data_list[i:i+12])
        lines.append(f'    {line}')
    
    c_array = f'// Original size: {len(original_data)} bytes'
    if is_compressed:
        c_array += f', Compressed size: {len(data)} bytes (ratio: {compression_ratio:.2f}x)\n'
    else:
        c_array += f' (not compressed)\n'
    
    c_array += f'const uint8_t {var_name}_data[] = {{\n'
    c_array += ',\n'.join(lines)
    c_array += '\n};\n'
    c_array += f'const size_t {var_name}_size = {len(data)};\n'
    c_array += f'const size_t {var_name}_original_size = {len(original_data)};\n'
    c_array += f'const bool {var_name}_is_compressed = {"true" if is_compressed else "false"};\n\n'
    
    return c_array, len(data), len(original_data), is_compressed

def get_content_type(file_ext):
    """파일 확장자에 따른 Content-Type 반환"""
    content_types = {
        '.html': 'text/html',
        '.css': 'text/css',
        '.js': 'application/javascript',
        '.json': 'application/json',
        '.png': 'image/png',
        '.jpg': 'image/jpeg',
        '.jpeg': 'image/jpeg',
        '.ico': 'image/x-icon',
        '.svg': 'image/svg+xml',
        '.woff': 'font/woff',
        '.woff2': 'font/woff2',
        '.ttf': 'font/ttf'
    }
    return content_types.get(file_ext.lower(), 'application/octet-stream')

def convert_spa_files(spa_folder, output_file, use_gzip=True):
    """SPA 폴더의 파일들을 C 코드로 변환 (Gzip 압축 옵션)"""
    spa_path = Path(spa_folder)
    if not spa_path.exists():
        print(f"Error: SPA folder '{spa_folder}' does not exist")
        return False
    
    # 헤더 파일 내용
    header_content = '''#ifndef STATIC_FILES_H
#define STATIC_FILES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// 임베드된 파일 구조체
typedef struct {
    const char* path;
    const char* content_type;
    const uint8_t* data;
    size_t size;
    size_t original_size;
    bool is_compressed;
} embedded_file_t;

'''
    
    # C 파일 내용
    c_content = '''#include "static_files.h"

'''
    
    # 파일 테이블 항목들
    file_entries = []
    var_declarations = []
    total_original_size = 0
    total_compressed_size = 0
    
    # SPA 폴더의 모든 파일 처리
    for file_path in spa_path.rglob('*'):
        if file_path.is_file():
            # 상대 경로 계산
            rel_path = file_path.relative_to(spa_path)
            web_path = '/' + str(rel_path).replace('\\', '/')
            
            # index.html을 루트 경로로도 매핑
            if rel_path.name == 'index.html':
                web_path_root = '/'
            else:
                web_path_root = None
            
            # 변수명 생성 (특수문자 제거)
            var_name = str(rel_path).replace('/', '_').replace('\\', '_').replace('.', '_').replace('-', '_')
            
            # 파일을 C 배열로 변환
            try:
                c_array, compressed_size, original_size, is_compressed = file_to_c_array(file_path, var_name, use_gzip)
                c_content += c_array
                
                total_original_size += original_size
                total_compressed_size += compressed_size
                
                # 헤더에 extern 선언 추가
                var_declarations.append(f'extern const uint8_t {var_name}_data[];')
                var_declarations.append(f'extern const size_t {var_name}_size;')
                var_declarations.append(f'extern const size_t {var_name}_original_size;')
                var_declarations.append(f'extern const bool {var_name}_is_compressed;')
                
                # Content-Type 결정
                content_type = get_content_type(file_path.suffix)
                
                # 파일 테이블 항목 추가
                file_entries.append(f'    {{"{web_path}", "{content_type}", {var_name}_data, {compressed_size}, {original_size}, {str(is_compressed).lower()}}}')
                
                # index.html인 경우 루트 경로도 추가
                if web_path_root:
                    file_entries.append(f'    {{"{web_path_root}", "{content_type}", {var_name}_data, {compressed_size}, {original_size}, {str(is_compressed).lower()}}}')
                
                compression_info = f" (compressed: {is_compressed}, ratio: {original_size/compressed_size:.2f}x)" if is_compressed else " (not compressed)"
                print(f"Converted: {rel_path} -> {var_name} ({original_size} -> {compressed_size} bytes){compression_info}")
                
            except Exception as e:
                print(f"Error converting {file_path}: {e}")
                continue
    
    # 헤더 파일 완성
    header_content += '\n'.join(var_declarations) + '\n\n'
    header_content += '''// 임베드된 파일 테이블
extern const embedded_file_t embedded_files[];
extern const size_t embedded_files_count;

// 함수 선언
const char* get_content_type(const char* file_path);
const char* get_embedded_file_with_content_type(const char* path, size_t* file_size, bool* is_compressed, size_t* original_size, const char** content_type);

#endif // STATIC_FILES_H
'''
    
    # C 파일에 파일 테이블 추가
    c_content += '// 임베드된 파일 테이블\n'
    c_content += 'const embedded_file_t embedded_files[] = {\n'
    c_content += ',\n'.join(file_entries)
    c_content += '\n};\n\n'
    c_content += f'const size_t embedded_files_count = {len(file_entries)};\n\n'
    
    # 유틸리티 함수들 추가
    c_content += '''#include <string.h>

// 파일 확장자에 따른 Content-Type 반환
const char* get_content_type(const char* file_path) {
    if (!file_path) return "application/octet-stream";
    
    const char* ext = strrchr(file_path, '.');
    if (!ext) return "application/octet-stream";
    
    if (strcmp(ext, ".html") == 0) return "text/html";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".js") == 0) return "application/javascript";
    if (strcmp(ext, ".json") == 0) return "application/json";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".ico") == 0) return "image/x-icon";
    if (strcmp(ext, ".svg") == 0) return "image/svg+xml";
    if (strcmp(ext, ".woff") == 0) return "font/woff";
    if (strcmp(ext, ".woff2") == 0) return "font/woff2";
    if (strcmp(ext, ".ttf") == 0) return "font/ttf";
    
    return "application/octet-stream";
}

// 임베드된 파일 찾기 (Content-Type 포함)
const char* get_embedded_file_with_content_type(const char* path, size_t* file_size, bool* is_compressed, size_t* original_size, const char** content_type) {
    if (!path || !file_size || !is_compressed || !original_size || !content_type) {
        return NULL;
    }
    
    for (size_t i = 0; i < embedded_files_count; i++) {
        if (strcmp(embedded_files[i].path, path) == 0) {
            *file_size = embedded_files[i].size;
            *original_size = embedded_files[i].original_size;
            *is_compressed = embedded_files[i].is_compressed;
            *content_type = embedded_files[i].content_type;
            return (const char*)embedded_files[i].data;
        }
    }
    
    return NULL;
}
'''
    
    # 파일 저장
    try:
        # static_files.h 저장
        with open(f'{output_file}.h', 'w', encoding='utf-8') as f:
            f.write(header_content)
        
        # static_files.c 저장
        with open(f'{output_file}.c', 'w', encoding='utf-8') as f:
            f.write(c_content)
        
        print(f"Successfully generated {output_file}.h and {output_file}.c")
        print(f"Total files converted: {len(file_entries)}")
        print(f"Total original size: {total_original_size:,} bytes")
        print(f"Total compressed size: {total_compressed_size:,} bytes")
        if total_original_size > 0:
            print(f"Overall compression ratio: {total_original_size/total_compressed_size:.2f}x")
            print(f"Space saved: {total_original_size - total_compressed_size:,} bytes ({(1-total_compressed_size/total_original_size)*100:.1f}%)")
        return True
        
    except Exception as e:
        print(f"Error writing output files: {e}")
        return False

def main():
    parser = argparse.ArgumentParser(description='Convert SPA build files to C code with Gzip compression')
    parser.add_argument('spa_folder', help='Path to SPA build folder (e.g., dist, build, web)')
    parser.add_argument('-o', '--output', default='static_files', help='Output filename prefix (default: static_files)')
    parser.add_argument('--no-gzip', action='store_true', help='Disable Gzip compression')
    
    args = parser.parse_args()
    
    if convert_spa_files(args.spa_folder, args.output, use_gzip=not args.no_gzip):
        print("\nConversion completed successfully!")
        print(f"Include the generated files in your CMakeLists.txt:")
        print(f"  {args.output}.c")
        print(f"Make sure to #include \"{args.output}.h\" in your HTTP server code.")
        if not args.no_gzip:
            print("Note: Files are Gzip compressed. Make sure your HTTP server supports decompression.")
    else:
        print("Conversion failed!")
        sys.exit(1)

if __name__ == '__main__':
    main()
