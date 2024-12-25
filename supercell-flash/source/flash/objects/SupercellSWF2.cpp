#include "SupercellSWF2.h"

#include "flash/objects/SupercellSWF.h"

#include <optional>

using namespace flatbuffers;

namespace sc::flash
{
	// SupercellSWF2CompileTable

	SupercellSWF2CompileTable::SupercellSWF2CompileTable(const SupercellSWF& _swf) : swf(_swf)
	{
		builder = FlatBufferBuilder(1024 * 1024, nullptr, false);
		//builder.TrackMinAlign(16);
	}

	void SupercellSWF2CompileTable::load_chunk(SupercellSWF& swf, const SC2::DataStorage* storage, const std::function<void(SupercellSWF&, const SC2::DataStorage*, const uint8_t*)>& reader)
	{
		uint32_t data_size = swf.stream.read_unsigned_int();
		reader(swf, storage, (uint8_t*)swf.stream.data() + swf.stream.position());
		swf.stream.seek(data_size, wk::Stream::SeekMode::Add);
	};

	uint32_t SupercellSWF2CompileTable::get_string_ref(const SWFString& target)
	{
		auto it = std::find(std::execution::par_unseq, strings.begin(), strings.end(), target);
		if (it != strings.end())
		{
			return (uint32_t)std::distance(strings.begin(), it);
		}
		else
		{
			uint32_t result = strings.size();
			strings.push_back(target);
			return result;
		}
	}

	uint32_t SupercellSWF2CompileTable::get_rect_ref(const wk::RectF& rect)
	{
		auto it = std::find_if(std::execution::par_unseq, rectangles.begin(), rectangles.end(), [&rect](const SC2::Typing::Rect& other)
			{
				return rect.left == other.left() && rect.right == other.right() && rect.top == other.top() && rect.bottom == other.bottom();
			}
		);

		if (it != rectangles.end())
		{
			return (uint32_t)std::distance(rectangles.begin(), it);
		}
		else
		{
			uint32_t result = rectangles.size();
			rectangles.emplace_back(rect.left, rect.top, rect.right, rect.bottom);
			return result;
		}
	}

	void SupercellSWF2CompileTable::gather_resources()
	{
		// Buffer preparing
		{
			size_t frame_elements_count = 0;
			for (const MovieClip& movie : swf.movieclips)
			{
				frame_elements_count += movie.frame_elements.size();
			}

			size_t shape_commands_size = 0;
			for (const Shape& shape : swf.shapes)
			{
				for (const ShapeDrawBitmapCommand& command : shape.commands)
				{
					shape_commands_size += command.vertices.size() * ShapeDrawBitmapCommandVertex::Size;
				}
			}

			frame_elements_indices.reserve(frame_elements_count);
			bitmaps_buffer.reserve(shape_commands_size);

			exports_ref_indices.reserve(swf.exports.size());
			textfields_ref_indices.reserve(swf.textfields.size());
			movieclips_ref_indices.reserve(swf.movieclips.size());
		}
		
		// Exports
		{
			for (const ExportName& export_name : swf.exports)
			{
				exports_ref_indices.push_back(
					get_string_ref(export_name.name)
				);
			}
		}

		// TextFields
		{
			for (const TextField& textField : swf.textfields)
			{
				RefT font_name = get_string_ref(textField.font_name);
				RefT text = get_string_ref(textField.text);
				RefT style_path = get_string_ref(textField.typography_file);

				textfields_ref_indices.emplace_back(font_name, text, style_path);
			}
		}

		// Shapes
		{
			uint32_t bitmap_points_counter = 0;
			for (const Shape& shape : swf.shapes)
			{
				for (const ShapeDrawBitmapCommand& command : shape.commands)
				{
					bitmaps_offsets.push_back(bitmap_points_counter);
					command.write_buffer(bitmaps_buffer, false, true);

					bitmap_points_counter += command.vertices.size();
				}
			}
		}

		// MovieClips
		{
			RefT frame_elements_counter = 0;
			for (const MovieClip& movieclip : swf.movieclips)
			{
				std::optional<RefArray<RefT>> children_names;

				bool has_children_names = std::any_of(movieclip.childrens.begin(), movieclip.childrens.end(),
					[](const DisplayObjectInstance& obj) {
						return !obj.name.empty();
					}
				);
				if (has_children_names)
				{
					children_names = RefArray<RefT>();
					children_names->reserve(movieclip.childrens.size());
					for (const DisplayObjectInstance& instance : movieclip.childrens)
					{
						children_names->push_back(
							get_string_ref(instance.name)
						);
					}
				}

				RefT frame_elements_offset = frame_elements_counter;
				for (const MovieClipFrameElement& element : movieclip.frame_elements)
				{
					frame_elements_indices.push_back(element.instance_index);
					frame_elements_indices.push_back(element.matrix_index);
					frame_elements_indices.push_back(element.colorTransform_index);
				}
				frame_elements_counter += movieclip.frame_elements.size() * 3;

				RefArray<RefT> frame_labels;
				frame_labels.reserve(movieclip.frames.size());

				for (const MovieClipFrame& frame : movieclip.frames)
				{
					frame_labels.push_back(
						get_string_ref(frame.label)
					);
				}

				std::optional<RefT> scaling_grid = std::nullopt;
				if (movieclip.scaling_grid.has_value())
				{
					scaling_grid = get_rect_ref(
						movieclip.scaling_grid.value()
					);
				}

				movieclips_ref_indices.emplace_back(children_names, frame_elements_offset, frame_labels, scaling_grid);
			}
		}

		// Textures
		{ }
	}

	void SupercellSWF2CompileTable::save_header()
	{
		Offset<Vector<Offset<String>>> strings_off = 0;
		Offset<Vector<const SC2::Typing::Rect*>> rects_off = 0;
		Offset<Vector<uint16_t>> movieclip_frame_elements_off = 0;
		Offset<Vector<uint8_t>> shape_bitmaps_off = 0;
		Offset<Vector<Offset<SC2::MatrixBank>>> banks_off = 0;

		// Strings
		if (!strings.empty())
		{
			RefArray<Offset<String>> raw_strings_off;
			raw_strings_off.reserve(strings.size());

			for (const auto& string : strings)
			{
				Offset<String> string_off = builder.CreateString(string.data(), string.length());
				raw_strings_off.push_back(string_off);

			}
			strings_off = builder.CreateVector(raw_strings_off);
		}

		// Rectangles
		if (!rectangles.empty())
		{
			rects_off = builder.CreateVectorOfStructs(rectangles);
		}

		// MovieClip frame elements
		{
			movieclip_frame_elements_off = builder.CreateVector(frame_elements_indices);
		}

		// Shape bitmaps vertices
		{
			shape_bitmaps_off = builder.CreateVector((uint8_t*)bitmaps_buffer.data(), bitmaps_buffer.length());
		}

		// Matrix Banks
		{
			RefArray<Offset<SC2::MatrixBank>> raw_banks_off;
			raw_banks_off.reserve(swf.matrixBanks.size());

			for (const MatrixBank& bank : swf.matrixBanks)
			{
				std::vector<SC2::Typing::Matrix2x3> matrices;
				std::vector<SC2::Typing::ColorTransform> colors;

				for (const flash::Matrix2D& matrix : bank.matrices)
				{
					matrices.emplace_back(matrix.a, matrix.b, matrix.c, matrix.d, matrix.tx, matrix.ty);
				}

				for (const flash::ColorTransform& color : bank.color_transforms)
				{
					colors.emplace_back(
						color.multiply.r, color.multiply.g, color.multiply.b, 
						color.alpha, 
						color.add.r, color.add.g, color.add.b
					);
				}

				auto bank_off = SC2::CreateMatrixBankDirect(builder, &matrices, &colors);
				raw_banks_off.push_back(bank_off);
			}

			banks_off = builder.CreateVector(raw_banks_off);
		}

		Offset<SC2::DataStorage> root_off = SC2::CreateDataStorage(
			builder, strings_off, 0, 0, rects_off, 
			movieclip_frame_elements_off, shape_bitmaps_off, banks_off
		);
		
		flush_builder(root_off);
	}

	void SupercellSWF2CompileTable::save_exports()
	{
		std::vector<uint16_t> exports_ids;
		exports_ids.reserve(swf.exports.size());
		for (const ExportName& name : swf.exports)
		{
			exports_ids.push_back(name.id);
		}

		Offset<SC2::ExportNames> root_off = SC2::CreateExportNamesDirect(
			builder, &exports_ids, &exports_ref_indices
		);

		flush_builder(root_off);
	}

	void SupercellSWF2CompileTable::save_textFields()
	{
		Offset<Vector<const SC2::TextField*>> textfields_offs;

		{
			std::vector<SC2::TextField> textfields_data;
			for (uint32_t i = 0; swf.textfields.size() > i; i++)
			{
				const TextField& textfield = swf.textfields[i];
				auto& [font_name, text, style_path] = textfields_ref_indices[i];

				SC2::TextField textfield_data(
					textfield.id, 0, font_name,
					textfield.left, textfield.top, textfield.right, textfield.bottom,
					textfield.font_color, textfield.outline_color, text, style_path,
					textfield.get_style_flags(), textfield.get_align_flags(),
					textfield.font_size, 0, textfield.unknown_short
				);
				
				textfields_data.push_back(textfield_data);
			}

			if (!swf.textfields.empty())
			{
				textfields_offs = builder.CreateVectorOfStructs(textfields_data);
			}
		}

		Offset<SC2::TextFields> root_off = SC2::CreateTextFields(
			builder, textfields_offs
		);

		flush_builder(root_off);
	}

	void SupercellSWF2CompileTable::save_shapes()
	{
		size_t command_counter = 0;
		Offset<Vector<Offset<sc::flash::SC2::Shape>>> shapes_off;
		{
			std::vector<Offset<sc::flash::SC2::Shape>> raw_shapes_off;

			for (const Shape& shape : swf.shapes)
			{
				std::vector<sc::flash::SC2::ShapeDrawBitmapCommand> commands;
				for (const ShapeDrawBitmapCommand& command : shape.commands)
				{
					uint32_t bitmap_data_offset = bitmaps_offsets[command_counter++];

					commands.emplace_back(
						0, command.texture_index, (uint32_t)command.vertices.size(), bitmap_data_offset
					);
				}

				Offset<sc::flash::SC2::Shape> shape_off;
				shape_off = SC2::CreateShapeDirect(builder, 
					shape.id, &commands
				);
				raw_shapes_off.push_back(shape_off);
			}

			if (!swf.shapes.empty())
			{
				shapes_off = builder.CreateVector(raw_shapes_off);
			}
		}

		Offset<SC2::Shapes> root_off = SC2::CreateShapes(
			builder, shapes_off
		);

		flush_builder(root_off);
	}

	void SupercellSWF2CompileTable::save_movieClips()
	{
		std::unordered_map<uint16_t, uint32_t> export_names;
		for (uint32_t i = 0; swf.exports.size() > i; i++)
		{
			const ExportName& export_name = swf.exports[i];
			export_names[export_name.id] = exports_ref_indices[i];
		}

		Offset<Vector<Offset<sc::flash::SC2::MovieClip>>> movieclips_off;
		{
			std::vector<Offset<sc::flash::SC2::MovieClip>> raw_movieclips_off;

			for (uint32_t i = 0; swf.movieclips.size() > i; i++)
			{
				auto& [children_names_data, frame_elements_offset, frame_labels, scaling_grid_index] = movieclips_ref_indices[i];
				const MovieClip& movieclip = swf.movieclips[i];

				std::vector<uint16_t> children_ids;
				std::vector<uint8_t> children_blending;
				children_ids.reserve(movieclip.childrens.size());
				children_blending.reserve(movieclip.childrens.size());

				std::transform(movieclip.childrens.begin(), movieclip.childrens.end(), std::back_inserter(children_ids),
					[](const auto& obj) { return obj.id; });

				std::transform(movieclip.childrens.begin(), movieclip.childrens.end(), std::back_inserter(children_blending),
					[](const auto& obj) { return (uint8_t)obj.blend_mode; });

				Optional<uint32_t> export_name_ref = nullopt;
				if (export_names.count(movieclip.id))
				{
					export_name_ref = export_names[movieclip.id];
				}

				std::vector<uint32_t>* childrens_names = nullptr;
				if (children_names_data.has_value())
				{
					childrens_names = &children_names_data.value();
				}

				std::vector<SC2::MovieClipFrame> frames;
				frames.reserve(movieclip.frames.size());

				for (uint16_t t = 0; movieclip.frames.size() > t; t++)
				{
					const MovieClipFrame& frame = movieclip.frames[t];
					frames.emplace_back(frame.elements_count, frame_labels[t]);
				}

				Optional<uint32_t> scaling_grid = nullopt;
				if (scaling_grid_index.has_value())
				{
					scaling_grid = scaling_grid_index.value();
				}

				Offset<sc::flash::SC2::MovieClip> movieclip_off;
				movieclip_off = SC2::CreateMovieClipDirect(
					builder, movieclip.id,
					export_name_ref,
					movieclip.frame_rate, movieclip.frames.size(), movieclip.unknown_flag,
					children_ids.empty() ? nullptr : &children_ids, 
					childrens_names, 
					children_blending.empty() ? nullptr : &children_blending,
					frames.empty() ? nullptr : &frames,
					frame_elements_offset, movieclip.bank_index, scaling_grid
				);
				raw_movieclips_off.push_back(movieclip_off);
			}

			if (!swf.movieclips.empty())
			{
				movieclips_off = builder.CreateVector(raw_movieclips_off);
			}
		}

		Offset<SC2::MovieClips> root_off = SC2::CreateMovieClips(
			builder, movieclips_off
		);

		flush_builder(root_off);
	}

	void SupercellSWF2CompileTable::save_modifiers()
	{
		Offset<Vector<const SC2::MovieClipModifier*>> modifiers_offs;
		{
			std::vector<SC2::MovieClipModifier> modifiers_data;
			for (const MovieClipModifier& modifier : swf.movieclip_modifiers)
			{
				modifiers_data.emplace_back(modifier.id, (uint8_t)modifier.type);
			}

			if (!swf.movieclip_modifiers.empty())
			{
				modifiers_offs = builder.CreateVectorOfStructs(modifiers_data);
			}
		}

		Offset <SC2::MovieClipModifiers > root_off = SC2::CreateMovieClipModifiers(
			builder, modifiers_offs
		);

		flush_builder(root_off);
	}

	uint32_t SupercellSWF2CompileTable::save_textures()
	{
		Offset<Vector<Offset<SC2::TextureSet>>> textures_offs;
		{
			std::vector<Offset<sc::flash::SC2::TextureSet>> raw_textures_offs;

			for (const SWFTexture& texture : swf.textures)
			{
				Offset<sc::flash::SC2::TextureSet> texuture_set_off;
				Offset<sc::flash::SC2::TextureData> highres_texuture_off;
				Offset<sc::flash::SC2::TextureData> lowres_texuture_off;

				// Warning. Big amount of hardcode.

				// Temporarly solution
				SWFTexture texture_copy = texture;
				texture_copy.encoding(SWFTexture::TextureEncoding::KhronosTexture);

				if (swf.use_low_resolution)
				{
					Offset<Vector<uint8_t>> texture_data_off;
					wk::BufferStream lowres_texture_data;
					texture_copy.save_buffer(lowres_texture_data, true);

					texture_data_off = builder.CreateVector((uint8_t*)lowres_texture_data.data(), lowres_texture_data.length());
					lowres_texuture_off = SC2::CreateTextureData(
						builder, 8, 0,
						(uint16_t)(round(texture_copy.image()->width() / 2)),
						(uint16_t)(round(texture_copy.image()->height() / 2)),
						texture_data_off
					);
				}

				{
					Offset<Vector<uint8_t>> texture_data_off;
					wk::BufferStream highres_texture_data;
					texture_copy.save_buffer(highres_texture_data, false);

					texture_data_off = builder.CreateVector((uint8_t*)highres_texture_data.data(), highres_texture_data.length());
					highres_texuture_off = SC2::CreateTextureData(
						builder, 8, 0,
						texture_copy.image()->width(),
						texture_copy.image()->height(),
						texture_data_off
					);
				}

				texuture_set_off = SC2::CreateTextureSet(
					builder, lowres_texuture_off, highres_texuture_off
				);
				raw_textures_offs.push_back(texuture_set_off);
			}

			if (!swf.textures.empty())
			{
				textures_offs = builder.CreateVector(raw_textures_offs);
			}
		}

		Offset <SC2::Textures > root_off = SC2::CreateTextures(
			builder, textures_offs
		);

		return (uint32_t)flush_builder(root_off);
	}

	void SupercellSWF2CompileTable::save_buffer()
	{
		// just a mini optimization
		get_string_ref("");

		gather_resources();

		header_offset = (uint32_t)swf.stream.position();
		save_header();

		data_offset = (uint32_t)swf.stream.position();
		save_exports();
		save_textFields();
		save_shapes();
		save_movieClips();
		save_modifiers();

		textures_length = save_textures();
	}

	void SupercellSWF2CompileTable::save_descriptor(wk::Stream& stream)
	{
		Offset<Vector<Offset<SC2::ExportNameHash>>> exports_hash_off = 0;
		{
			std::vector<Offset<SC2::ExportNameHash>> exports_hashes_offs;
			for (const ExportName& export_name : swf.exports)
			{
				if (export_name.name.empty()) continue;

				Offset<Vector<uint8_t>> export_name_hash_off = 0;
				Offset<String> export_name_off = 0;

				if (!export_name.hash.empty())
				{
					export_name_hash_off = builder.CreateVector(export_name.hash.data(), export_name.hash.size());
				}
				
				export_name_off = builder.CreateString(export_name.name.data(), export_name.name.length());

				Offset<SC2::ExportNameHash> name_off = SC2::CreateExportNameHash(
					builder, export_name_off, export_name_hash_off
				);

				exports_hashes_offs.push_back(name_off);
			}

			exports_hash_off = builder.CreateVector(exports_hashes_offs);
		}

		Offset<SC2::FileDescriptor> root_off = SC2::CreateFileDescriptor(
			builder, 1, 1,
			swf.shapes.size(), swf.movieclips.size(), swf.textures.size(), swf.textfields.size(), 0,
			header_offset, data_offset, textures_length, exports_hash_off
		);

		builder.FinishSizePrefixed(root_off);
		auto buffer = builder.GetBufferSpan();
		stream.write(buffer.data(), buffer.size_bytes());

		builder.Clear();
	}
}