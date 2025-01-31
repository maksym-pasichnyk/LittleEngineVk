#include <levk/core/io/file_monitor.hpp>
#include <levk/core/log.hpp>
#include <levk/core/utils/error.hpp>
#include <levk/core/utils/string.hpp>
#include <filesystem>

namespace le::io {
namespace stdfs = std::filesystem;

namespace {
bool rf([[maybe_unused]] stdfs::path const& path, [[maybe_unused]] std::error_code& err_code) { return stdfs::is_regular_file(path, err_code); }

stdfs::file_time_type lwt([[maybe_unused]] stdfs::path const& path, [[maybe_unused]] std::error_code& err_code) {
	return stdfs::last_write_time(path, err_code);
}
} // namespace

FileMonitor::FileMonitor(Path path, Mode mode) : m_path(std::move(path)), m_mode(mode) {}

FileMonitor::Status FileMonitor::update() {
	std::error_code errCode;
	stdfs::path const path(m_path.generic_string());
	if (rf(path, errCode)) {
		auto const lastWriteTime = lwt(path, errCode);
		if (errCode) { return m_status; }
		if (lastWriteTime != m_lastWriteTime || m_status == Status::eNotFound) {
			bool bDirty = m_lastWriteTime != stdfs::file_time_type();
			m_lastWriteTime = lastWriteTime;
			if (m_mode == Mode::eTextContents) {
				if (auto text = s_fsMedia.string(m_path.generic_string())) {
					if (m_payload.contains<std::string>() && *text == m_payload.get<std::string>()) {
						bDirty = false;
					} else {
						m_payload = std::move(text).value();
						m_lastModifiedTime = m_lastWriteTime;
					}
				}
			} else if (m_mode == Mode::eBinaryContents) {
				if (auto bytes = s_fsMedia.bytes(m_path.generic_string())) {
					if (m_payload.contains<bytearray>() && *bytes == m_payload.get<bytearray>()) {
						bDirty = false;
					} else {
						m_payload = std::move(bytes).value();
						m_lastModifiedTime = m_lastWriteTime;
					}
				}
			}
			m_status = bDirty ? Status::eModified : Status::eUpToDate;
		} else {
			m_status = Status::eUpToDate;
		}
	} else {
		m_status = Status::eNotFound;
	}
	return m_status;
}

std::string_view FileMonitor::text() const {
	ENSURE(m_mode == Mode::eTextContents, "Monitor not in Text Contents mode!");
	if (m_mode != Mode::eTextContents) {
		logE("[{}] not monitoring file contents (only timestamp) [{}]!", utils::tName<FSMedia>(), m_path.generic_string());
		return {};
	}
	return m_payload.get<std::string>();
}

Span<std::byte const> FileMonitor::bytes() const {
	ENSURE(m_mode == Mode::eBinaryContents, "Monitor not in Text Contents mode!");
	if (m_mode != Mode::eBinaryContents) {
		logE("[{}] not monitoring file contents (only timestamp) [{}]!", utils::tName<FSMedia>(), m_path.generic_string());
		return {};
	}
	return m_payload.get<bytearray>();
}
} // namespace le::io
