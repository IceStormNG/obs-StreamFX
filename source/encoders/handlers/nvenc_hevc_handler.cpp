// FFMPEG Video Encoder Integration for OBS Studio
// Copyright (c) 2019 Michael Fabian Dirks <info@xaymar.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "nvenc_hevc_handler.hpp"
#include "strings.hpp"
#include "../codecs/hevc.hpp"
#include "../encoder-ffmpeg.hpp"
#include "ffmpeg/tools.hpp"
#include "nvenc_shared.hpp"
#include "plugin.hpp"

extern "C" {
#include <obs-module.h>
#pragma warning(push)
#pragma warning(disable : 4242 4244 4365)
#include <libavutil/opt.h>
#pragma warning(pop)
}

#define ST_KEY_PROFILE "H265.Profile"
#define ST_KEY_TIER "H265.Tier"
#define ST_KEY_LEVEL "H265.Level"

using namespace streamfx::encoder::ffmpeg::handler;
using namespace streamfx::encoder::codec::hevc;

void nvenc_hevc_handler::adjust_info(ffmpeg_factory* fac, const AVCodec*, std::string&, std::string& name, std::string&)
{
	name = "NVIDIA NVENC H.265/HEVC (via FFmpeg)";
	if (!nvenc::is_available())
		fac->get_info()->caps |= OBS_ENCODER_CAP_DEPRECATED;
}

void nvenc_hevc_handler::get_defaults(obs_data_t* settings, const AVCodec* codec, AVCodecContext* context, bool)
{
	nvenc::get_defaults(settings, codec, context);

	obs_data_set_default_string(settings, ST_KEY_PROFILE, "");
	obs_data_set_default_string(settings, ST_KEY_TIER, "");
	obs_data_set_default_string(settings, ST_KEY_LEVEL, "auto");
}

bool nvenc_hevc_handler::has_keyframe_support(ffmpeg_factory*)
{
	return true;
}

bool nvenc_hevc_handler::is_hardware_encoder(ffmpeg_factory* instance)
{
	return true;
}

bool nvenc_hevc_handler::has_threading_support(ffmpeg_factory* instance)
{
	return false;
}

bool nvenc_hevc_handler::has_pixel_format_support(ffmpeg_factory* instance)
{
	return true;
}

void nvenc_hevc_handler::get_properties(obs_properties_t* props, const AVCodec* codec, AVCodecContext* context, bool)
{
	if (!context) {
		this->get_encoder_properties(props, codec);
	} else {
		this->get_runtime_properties(props, codec, context);
	}
}

void nvenc_hevc_handler::update(obs_data_t* settings, const AVCodec* codec, AVCodecContext* context)
{
	nvenc::update(settings, codec, context);

	if (!context->internal) {
		if (const char* v = obs_data_get_string(settings, ST_KEY_PROFILE); v && (v[0] != '\0')) {
			av_opt_set(context->priv_data, "profile", v, AV_OPT_SEARCH_CHILDREN);
		}
		if (const char* v = obs_data_get_string(settings, ST_KEY_TIER); v && (v[0] != '\0')) {
			av_opt_set(context->priv_data, "tier", v, AV_OPT_SEARCH_CHILDREN);
		}
		if (const char* v = obs_data_get_string(settings, ST_KEY_LEVEL); v && (v[0] != '\0')) {
			av_opt_set(context->priv_data, "level", v, AV_OPT_SEARCH_CHILDREN);
		}
	}
}

void nvenc_hevc_handler::override_update(ffmpeg_instance* instance, obs_data_t* settings)
{
	nvenc::override_update(instance, settings);
}

void nvenc_hevc_handler::log_options(obs_data_t* settings, const AVCodec* codec, AVCodecContext* context)
{
	nvenc::log_options(settings, codec, context);

	DLOG_INFO("[%s]     H.265/HEVC:", codec->name);
	::streamfx::ffmpeg::tools::print_av_option_string2(context, "profile", "      Profile",
													   [](int64_t v, std::string_view o) { return std::string(o); });
	::streamfx::ffmpeg::tools::print_av_option_string2(context, "level", "      Level",
													   [](int64_t v, std::string_view o) { return std::string(o); });
	::streamfx::ffmpeg::tools::print_av_option_string2(context, "tier", "      Tier",
													   [](int64_t v, std::string_view o) { return std::string(o); });
}

void nvenc_hevc_handler::get_encoder_properties(obs_properties_t* props, const AVCodec* codec)
{
	AVCodecContext* context = avcodec_alloc_context3(codec);
	if (!context->priv_data) {
		avcodec_free_context(&context);
		return;
	}

	nvenc::get_properties_pre(props, codec, context);

	{
		obs_properties_t* grp = props;
		if (!streamfx::util::are_property_groups_broken()) {
			grp = obs_properties_create();
			obs_properties_add_group(props, S_CODEC_HEVC, D_TRANSLATE(S_CODEC_HEVC), OBS_GROUP_NORMAL, grp);
		}

		{
			auto p = obs_properties_add_list(grp, ST_KEY_PROFILE, D_TRANSLATE(S_CODEC_HEVC_PROFILE),
											 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
			obs_property_list_add_int(p, D_TRANSLATE(S_STATE_DEFAULT), -1);
			streamfx::ffmpeg::tools::avoption_list_add_entries(
				context->priv_data, "profile", [&p](const AVOption* opt) {
					char buffer[1024];
					snprintf(buffer, sizeof(buffer), "%s.%s\0", S_CODEC_HEVC_PROFILE, opt->name);
					obs_property_list_add_string(p, D_TRANSLATE(buffer), opt->name);
				});
		}
		{
			auto p = obs_properties_add_list(grp, ST_KEY_TIER, D_TRANSLATE(S_CODEC_HEVC_TIER), OBS_COMBO_TYPE_LIST,
											 OBS_COMBO_FORMAT_STRING);
			obs_property_list_add_int(p, D_TRANSLATE(S_STATE_DEFAULT), -1);
			streamfx::ffmpeg::tools::avoption_list_add_entries(context->priv_data, "tier", [&p](const AVOption* opt) {
				char buffer[1024];
				snprintf(buffer, sizeof(buffer), "%s.%s\0", S_CODEC_HEVC_TIER, opt->name);
				obs_property_list_add_string(p, D_TRANSLATE(buffer), opt->name);
			});
		}
		{
			auto p = obs_properties_add_list(grp, ST_KEY_LEVEL, D_TRANSLATE(S_CODEC_HEVC_LEVEL), OBS_COMBO_TYPE_LIST,
											 OBS_COMBO_FORMAT_STRING);

			streamfx::ffmpeg::tools::avoption_list_add_entries(context->priv_data, "level", [&p](const AVOption* opt) {
				if (opt->default_val.i64 == 0) {
					obs_property_list_add_string(p, D_TRANSLATE(S_STATE_AUTOMATIC), "auto");
				} else {
					obs_property_list_add_string(p, opt->name, opt->name);
				}
			});
		}
	}

	nvenc::get_properties_post(props, codec, context);

	if (context) {
		avcodec_free_context(&context);
	}
}

void nvenc_hevc_handler::get_runtime_properties(obs_properties_t* props, const AVCodec* codec, AVCodecContext* context)
{
	nvenc::get_runtime_properties(props, codec, context);
}

void streamfx::encoder::ffmpeg::handler::nvenc_hevc_handler::migrate(obs_data_t* settings, uint64_t version,
																	 const AVCodec* codec, AVCodecContext* context)
{
	nvenc::migrate(settings, version, codec, context);

	if (version < STREAMFX_MAKE_VERSION(0, 11, 1, 0)) {
		// Profile
		if (auto v = obs_data_get_int(settings, ST_KEY_PROFILE); v != -1) {
			if (!obs_data_has_user_value(settings, ST_KEY_PROFILE))
				v = 0;

			std::map<int64_t, std::string> preset{
				{0, "main"},
				{1, "main10"},
				{2, "rext"},
			};
			if (auto k = preset.find(v); k != preset.end()) {
				obs_data_set_string(settings, ST_KEY_PROFILE, k->second.data());
			}
		}

		// Tier
		if (auto v = obs_data_get_int(settings, ST_KEY_TIER); v != -1) {
			if (!obs_data_has_user_value(settings, ST_KEY_TIER))
				v = 0;

			std::map<int64_t, std::string> preset{
				{0, "main"},
				{1, "high"},
			};
			if (auto k = preset.find(v); k != preset.end()) {
				obs_data_set_string(settings, ST_KEY_TIER, k->second.data());
			}
		}

		// Level
		obs_data_set_string(settings, ST_KEY_LEVEL, "auto");
	}
}

bool nvenc_hevc_handler::supports_reconfigure(ffmpeg_factory* instance, bool& threads, bool& gpu, bool& keyframes)
{
	threads   = false;
	gpu       = false;
	keyframes = false;
	return true;
}
