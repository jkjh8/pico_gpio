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
extern const uint8_t icon_svg_data[];
extern const size_t icon_svg_size;
extern const size_t icon_svg_original_size;
extern const bool icon_svg_is_compressed;
extern const uint8_t index_html_data[];
extern const size_t index_html_size;
extern const size_t index_html_original_size;
extern const bool index_html_is_compressed;
extern const uint8_t assets_axios_Xw0Q_U4f_js_data[];
extern const size_t assets_axios_Xw0Q_U4f_js_size;
extern const size_t assets_axios_Xw0Q_U4f_js_original_size;
extern const bool assets_axios_Xw0Q_U4f_js_is_compressed;
extern const uint8_t assets_ErrorNotFound_B6S7QrPl_js_data[];
extern const size_t assets_ErrorNotFound_B6S7QrPl_js_size;
extern const size_t assets_ErrorNotFound_B6S7QrPl_js_original_size;
extern const bool assets_ErrorNotFound_B6S7QrPl_js_is_compressed;
extern const uint8_t assets_GpioPage_CRBCvqIO_js_data[];
extern const size_t assets_GpioPage_CRBCvqIO_js_size;
extern const size_t assets_GpioPage_CRBCvqIO_js_original_size;
extern const bool assets_GpioPage_CRBCvqIO_js_is_compressed;
extern const uint8_t assets_index_DwWt3dB1_css_data[];
extern const size_t assets_index_DwWt3dB1_css_size;
extern const size_t assets_index_DwWt3dB1_css_original_size;
extern const bool assets_index_DwWt3dB1_css_is_compressed;
extern const uint8_t assets_index_j8Xb7bbI_js_data[];
extern const size_t assets_index_j8Xb7bbI_js_size;
extern const size_t assets_index_j8Xb7bbI_js_original_size;
extern const bool assets_index_j8Xb7bbI_js_is_compressed;
extern const uint8_t assets_IndexPage_TmCgYfxw_js_data[];
extern const size_t assets_IndexPage_TmCgYfxw_js_size;
extern const size_t assets_IndexPage_TmCgYfxw_js_original_size;
extern const bool assets_IndexPage_TmCgYfxw_js_is_compressed;
extern const uint8_t assets_MainLayout_DPUu8n8E_js_data[];
extern const size_t assets_MainLayout_DPUu8n8E_js_size;
extern const size_t assets_MainLayout_DPUu8n8E_js_original_size;
extern const bool assets_MainLayout_DPUu8n8E_js_is_compressed;
extern const uint8_t assets_QBtn_9L7WJJj8_js_data[];
extern const size_t assets_QBtn_9L7WJJj8_js_size;
extern const size_t assets_QBtn_9L7WJJj8_js_original_size;
extern const bool assets_QBtn_9L7WJJj8_js_is_compressed;
extern const uint8_t assets_use_model_toggle_l1Gyd5Y8_js_data[];
extern const size_t assets_use_model_toggle_l1Gyd5Y8_js_size;
extern const size_t assets_use_model_toggle_l1Gyd5Y8_js_original_size;
extern const bool assets_use_model_toggle_l1Gyd5Y8_js_is_compressed;
extern const uint8_t icons_favicon_128x128_png_data[];
extern const size_t icons_favicon_128x128_png_size;
extern const size_t icons_favicon_128x128_png_original_size;
extern const bool icons_favicon_128x128_png_is_compressed;
extern const uint8_t icons_favicon_16x16_png_data[];
extern const size_t icons_favicon_16x16_png_size;
extern const size_t icons_favicon_16x16_png_original_size;
extern const bool icons_favicon_16x16_png_is_compressed;
extern const uint8_t icons_favicon_32x32_png_data[];
extern const size_t icons_favicon_32x32_png_size;
extern const size_t icons_favicon_32x32_png_original_size;
extern const bool icons_favicon_32x32_png_is_compressed;
extern const uint8_t icons_favicon_96x96_png_data[];
extern const size_t icons_favicon_96x96_png_size;
extern const size_t icons_favicon_96x96_png_original_size;
extern const bool icons_favicon_96x96_png_is_compressed;

// 임베드된 파일 테이블
extern const embedded_file_t embedded_files[];
extern const size_t embedded_files_count;

// 함수 선언
const char* get_content_type(const char* file_path);
const char* get_embedded_file_with_content_type(const char* path, size_t* file_size, bool* is_compressed, size_t* original_size, const char** content_type);

#endif // STATIC_FILES_H
