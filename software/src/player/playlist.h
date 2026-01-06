#pragma once

#include <cstddef>

#ifdef __cplusplus
#include <vector>
#include <string>

// File entry (beat or sample)
struct sc_file {
	std::string full_path;
	unsigned int global_index = 0;  // Index across all files
};

// Folder containing files
struct sc_folder {
	std::string full_path;
	std::vector<sc_file> files;
};

/*
 * Playlist - manages a collection of folders and audio files
 *
 * Replaces the old C linked-list implementation with std::vector.
 * Provides O(1) random access by index and O(1) navigation.
 */
class Playlist {
public:
	Playlist() = default;
	~Playlist() = default;

	// Non-copyable, movable
	Playlist(const Playlist&) = delete;
	Playlist& operator=(const Playlist&) = delete;
	Playlist(Playlist&&) = default;
	Playlist& operator=(Playlist&&) = default;

	/*
	 * Load all audio files from a base folder
	 * Scans subdirectories for audio files, excluding .cue files
	 *
	 * Return: true if files were found, false otherwise
	 */
	bool load(const char* base_folder_path);

	/*
	 * Get file by global index (for random access / shuffle)
	 * Return: pointer to file, or nullptr if index out of range
	 */
	sc_file* get_file_at_index(unsigned int index);

	/*
	 * Get folder by index
	 * Return: pointer to folder, or nullptr if index out of range
	 */
	sc_folder* get_folder(size_t folder_idx);

	/*
	 * Get file within a folder by index
	 * Return: pointer to file, or nullptr if indices out of range
	 */
	sc_file* get_file(size_t folder_idx, size_t file_idx);

	// Counts
	size_t folder_count() const { return folders_.size(); }
	size_t total_files() const { return total_files_; }
	size_t file_count_in_folder(size_t folder_idx) const;

	// Check if a next/prev exists (for navigation bounds checking)
	bool has_next_file(size_t folder_idx, size_t file_idx) const;
	bool has_prev_file(size_t folder_idx, size_t file_idx) const;
	bool has_next_folder(size_t folder_idx) const;
	bool has_prev_folder(size_t folder_idx) const;

	// Debug output
	void dump() const;

private:
	std::vector<sc_folder> folders_;
	std::vector<sc_file*> all_files_;  // Flat view for O(1) random access
	size_t total_files_ = 0;
};

extern "C" {
#endif

// C-compatible wrapper functions (for any remaining C code)
// These are deprecated - prefer using Playlist class directly

#ifdef __cplusplus
}
#endif
