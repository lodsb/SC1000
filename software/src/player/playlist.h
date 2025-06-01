#pragma once

// Folders contain files
struct sc_folder {
	char full_path[256];
	struct sc_file*   first_file;
	struct sc_folder* next;
	struct sc_folder* prev;
};

// Struct to hold file (beat or sample)
struct sc_file {
	char full_path[256];
	unsigned int index;
	struct sc_file* next;
	struct sc_file* prev;
};

struct sc_file * get_file_at_index( unsigned int index, struct sc_folder *first_folder );

struct sc_folder * load_file_structure( char *base_folder_path, unsigned int *total_num );

void dump_file_structure( struct sc_folder *FirstFolder );
