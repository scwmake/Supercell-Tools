#pragma once

namespace sc
{
	class SupercellSWF;

	class SWFTexture
	{
		/* Functions */
	public:
		SWFTexture* load(SupercellSWF* swf, uint8_t tag, bool useExternalTexture);
		void save(SupercellSWF* swf, bool isExternal, bool isLowres);

		/* Enums */
	public:
		enum class Filter : uint8_t
		{
			LINEAR_NEAREST,
			NEAREST_NEAREST,
			LINEAR_MIPMAP_NEAREST
		};

		enum class PixelFormat : uint8_t
		{
			RGBA8 = 0,
			RGBA4 = 2,
			RGB5_A1,
			RGB565,
			LUMINANCE8_ALPHA8 = 6,
			LUMINANCE8 = 10,
			// PVR = 15
		};

		/* Static */
	public:
		static std::vector<PixelFormat> pixelFormatTable;
		static std::vector<uint8_t> pixelByteSizeTable;
		static std::vector<uint8_t> channelsCountTable;

		/* Getters */
	public:
		PixelFormat pixelFormat() { return m_pixelFormat; }

		Filter textureFilter() { return m_textureFilter; }

		uint16_t width() { return m_width; }
		uint16_t height() { return m_height; }

		bool linear() { return m_linear; }
		bool downscaling() { return m_downscaling; }

		/* Setters */
	public:
		void pixelFormat(PixelFormat type);

		void textureFilder(Filter filter) { m_textureFilter = filter; }

		void width(uint16_t width) { m_width = width; }
		void height(uint16_t height) { m_height = height; }

		void downscaling(bool status) { m_downscaling = status; }
		void linear(bool status);

		/* Vectors */
	public:
		/* Image data */
		std::vector<uint8_t> data;

		/* Some helper functions */
	public:
		static std::vector<uint8_t> getLinearData(SWFTexture& texture, bool toLinear);
		static std::vector<uint8_t> getPixelFormatData(SWFTexture& texture, PixelFormat dst);
		static std::vector<uint8_t> getPixelFormatData(uint8_t* data, uint16_t width , uint16_t height, PixelFormat srcType, PixelFormat dstType);

	private:
		PixelFormat m_pixelFormat = PixelFormat::RGBA8;

		Filter m_textureFilter = Filter::LINEAR_NEAREST;

		uint16_t m_width = 0;
		uint16_t m_height = 0;

		bool m_linear = false;
		bool m_downscaling = true;
	};
}
