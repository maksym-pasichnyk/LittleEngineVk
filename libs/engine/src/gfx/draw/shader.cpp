#include <cstdlib>
#include <fmt/format.h>
#include "core/assert.hpp"
#include "core/log.hpp"
#include "core/io.hpp"
#include "core/utils.hpp"
#include "gfx/utils.hpp"
#include "shader.hpp"

namespace le::gfx
{
std::string const Shader::s_tName = utils::tName<Shader>();
std::array<vk::ShaderStageFlagBits, size_t(ShaderType::eCOUNT_)> const Shader::s_typeToFlagBit = {vk::ShaderStageFlagBits::eVertex,
																								  vk::ShaderStageFlagBits::eFragment};

Shader::Shader(ShaderData data) : m_id(std::move(data.id))
{
	if (data.codeMap.empty())
	{
		ASSERT(!data.codeIDMap.empty() && data.pReader, "Invalid Shader ShaderData!");
		for (auto const& [type, id] : data.codeIDMap)
		{
			auto const ext = extension(id);
			if (ext == ".vert" || ext == ".frag")
			{
				if (!loadGlsl(data, id, type))
				{
					LOG_E("[{}] Failed to compile GLSL code to SPIR-V!", s_tName);
				}
			}
			else if (ext == ".spv")
			{
				auto [shaderShaderData, bResult] = data.pReader->getBytes(id);
				ASSERT(bResult, "Shader code missing!");
				data.codeMap[type] = std::move(shaderShaderData);
			}
			else
			{
				LOG_E("[{}] Unknown extension! [{}]", s_tName, ext);
			}
		}
	}
	loadAllSpirV(data.codeMap);
}

Shader::~Shader()
{
	for (auto const& shader : m_shaders)
	{
		vkDestroy(shader);
	}
}

vk::ShaderModule Shader::module(ShaderType type) const
{
	ASSERT(m_shaders.at((size_t)type) != vk::ShaderModule(), "Module not present in Shader!");
	return m_shaders.at((size_t)type);
}

std::unordered_map<ShaderType, vk::ShaderModule> Shader::modules() const
{
	std::unordered_map<ShaderType, vk::ShaderModule> ret;
	for (size_t idx = 0; idx < (size_t)ShaderType::eCOUNT_; ++idx)
	{
		auto const& module = m_shaders.at(idx);
		if (module != vk::ShaderModule())
		{
			ret[(ShaderType)idx] = module;
		}
	}
	return ret;
}

#if defined(LEVK_SHADER_HOT_RELOAD)
FileMonitor::Status Shader::currentStatus() const
{
	return m_lastStatus;
}

FileMonitor::Status Shader::update()
{
	bool bDirty = false;
	for (auto& monitor : m_monitors)
	{
		if (monitor.monitor.update() == FileMonitor::Status::eModified)
		{
			bDirty = true;
		}
	}
	if (bDirty)
	{
		std::unordered_map<ShaderType, bytearray> spvCode;
		for (auto& monitor : m_monitors)
		{
			if (monitor.monitor.lastStatus() == FileMonitor::Status::eModified)
			{
				if (!glslToSpirV(monitor.id, spvCode[monitor.type], monitor.pReader))
				{
					LOG_E("[{}] Failed to reload Shader!", s_tName);
					return m_lastStatus = FileMonitor::Status::eUpToDate;
				}
			}
			else
			{
				auto spvID = monitor.id;
				spvID += ShaderCompiler::s_extension;
				auto [bytes, bResult] = monitor.pReader->getBytes(spvID);
				if (!bResult)
				{
					LOG_E("[{}] Failed to reload Shader!", s_tName);
					return m_lastStatus = FileMonitor::Status::eUpToDate;
				}
				spvCode[monitor.type] = std::move(bytes);
			}
		}
		LOG_D("[{}] Reloading...", s_tName);
		for (auto shader : m_shaders)
		{
			vkDestroy(shader);
		}
		loadAllSpirV(spvCode);
		LOG_I("[{}] Reloaded", s_tName);
		return m_lastStatus = FileMonitor::Status::eModified;
	}
	return m_lastStatus = FileMonitor::Status::eUpToDate;
}
#endif

bool Shader::loadGlsl(ShaderData& out_data, stdfs::path const& id, ShaderType type)
{
	if (ShaderCompiler::instance().status() != ShaderCompiler::Status::eOnline)
	{
		LOG_E("[{}] ShaderCompiler is Offline!", s_tName);
		return false;
	}
	auto pFileReader = dynamic_cast<FileReader const*>(out_data.pReader);
	ASSERT(pFileReader, "Cannot compile shaders without FileReader!");
	auto [glslCode, bResult] = pFileReader->getString(id);
	if (bResult)
	{
		if (glslToSpirV(id, out_data.codeMap[type], pFileReader))
		{
#if defined(LEVK_SHADER_HOT_RELOAD)
			m_monitors.push_back({id, FileMonitor(pFileReader->fullPath(id), FileMonitor::Mode::eContents), type, pFileReader});
#endif
			return true;
		}
	}
	return false;
}

bool Shader::glslToSpirV(stdfs::path const& id, bytearray& out_bytes, FileReader const* pReader)
{
	if (ShaderCompiler::instance().status() != ShaderCompiler::Status::eOnline)
	{
		LOG_E("[{}] ShaderCompiler is Offline!", s_tName);
		return false;
	}
	ASSERT(pReader, "Cannot compile shaders without FileReader!");
	auto [glslCode, bResult] = pReader->getString(id);
	if (bResult)
	{
		auto const src = pReader->fullPath(id);
		auto dstID = id;
		dstID += ShaderCompiler::s_extension;
		if (!ShaderCompiler::instance().compile(src, true))
		{
			return false;
		}
		auto [spvCode, bResult] = pReader->getBytes(dstID);
		if (!bResult)
		{
			return false;
		}
		out_bytes = std::move(spvCode);
		return true;
	}
	return false;
}

void Shader::loadAllSpirV(std::unordered_map<ShaderType, bytearray> const& byteMap)
{
	std::array<vk::ShaderModule, (size_t)ShaderType::eCOUNT_> newModules;
	for (auto const& [type, code] : byteMap)
	{
		vk::ShaderModuleCreateInfo createInfo;
		createInfo.codeSize = code.size();
		createInfo.pCode = reinterpret_cast<u32 const*>(code.data());
		newModules.at((size_t)type) = g_info.device.createShaderModule(createInfo);
	}
	m_shaders = newModules;
}

std::string Shader::extension(stdfs::path const& id)
{
	auto const str = id.generic_string();
	if (auto idx = str.find_last_of('.'); idx != std::string::npos)
	{
		return str.substr(idx);
	}
	return {};
}

std::string const ShaderCompiler::s_tName = utils::tName<ShaderCompiler>();
std::string_view ShaderCompiler::s_extension = ".spv";

ShaderCompiler& ShaderCompiler::instance()
{
	static ShaderCompiler s_instance;
	return s_instance;
}

ShaderCompiler::ShaderCompiler()
{
	if (std::system("glslc --version") != 0)
	{
		LOG_E("[{}] Failed to go Online!", s_tName);
	}
	else
	{
		m_status = Status::eOnline;
		LOG_I("[{}] Online", s_tName);
	}
}

ShaderCompiler::~ShaderCompiler() = default;

ShaderCompiler::Status ShaderCompiler::status() const
{
	return m_status;
}

bool ShaderCompiler::compile(stdfs::path const& src, stdfs::path const& dst, bool bOverwrite)
{
	if (!statusCheck())
	{
		return false;
	}
	if (!stdfs::is_regular_file(src))
	{
		LOG_E("[{}] Failed to find source file: [{}]", s_tName, src.generic_string());
		return false;
	}
	if (stdfs::is_regular_file(dst) && !bOverwrite)
	{
		LOG_E("[{}] Destination file exists and overwrite flag not set: [{}]", s_tName, src.generic_string());
		return false;
	}
	auto const command = fmt::format("glslc {} -o {}", src.string(), dst.string());
	if (std::system(command.data()) != 0)
	{
		LOG_E("[{}] Shader compilation failed: [{}]", src.generic_string());
		return false;
	}
	LOG_I("[{}] [{}] => [{}] compiled successfully", s_tName, src.generic_string(), dst.generic_string());
	return true;
}

bool ShaderCompiler::compile(stdfs::path const& src, bool bOverwrite)
{
	auto dst = src;
	dst += s_extension;
	return compile(src, dst, bOverwrite);
}

bool ShaderCompiler::statusCheck() const
{
	if (m_status != Status::eOnline)
	{
		LOG_E("[{}] Not Online!", s_tName);
		return false;
	}
	return true;
}
} // namespace le::gfx