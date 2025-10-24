#ifndef STATIC_FILES_H
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

extern const uint8_t favicon_ico_data[];
extern const size_t favicon_ico_size;
extern const size_t favicon_ico_original_size;
extern const bool favicon_ico_is_compressed;
extern const uint8_t index_html_data[];
extern const size_t index_html_size;
extern const size_t index_html_original_size;
extern const bool index_html_is_compressed;
extern const uint8_t assets_index_CHxVr_vP_js_data[];
extern const size_t assets_index_CHxVr_vP_js_size;
extern const size_t assets_index_CHxVr_vP_js_original_size;
extern const bool assets_index_CHxVr_vP_js_is_compressed;
extern const uint8_t assets_index_CHxVr_vP_js_gz_data[];
extern const size_t assets_index_CHxVr_vP_js_gz_size;
extern const size_t assets_index_CHxVr_vP_js_gz_original_size;
extern const bool assets_index_CHxVr_vP_js_gz_is_compressed;
extern const uint8_t assets_index_DPhrYxVh_css_data[];
extern const size_t assets_index_DPhrYxVh_css_size;
extern const size_t assets_index_DPhrYxVh_css_original_size;
extern const bool assets_index_DPhrYxVh_css_is_compressed;
extern const uint8_t assets_index_DPhrYxVh_css_gz_data[];
extern const size_t assets_index_DPhrYxVh_css_gz_size;
extern const size_t assets_index_DPhrYxVh_css_gz_original_size;
extern const bool assets_index_DPhrYxVh_css_gz_is_compressed;

// 임베드된 파일 테이블
extern const embedded_file_t embedded_files[];
extern const size_t embedded_files_count;

// 함수 선언
const char* get_content_type(const char* file_path);
const char* get_embedded_file_with_content_type(const char* path, size_t* file_size, bool* is_compressed, size_t* original_size, const char** content_type);

#endif // STATIC_FILES_H
