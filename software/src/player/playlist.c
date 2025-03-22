// SC1000 playlist routines
// Build a linked-list tree of every track on the usb stick


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdint.h>
#include <stdbool.h>
#include <dirent.h>
#include <string.h>
#include "playlist.h"

struct sc_file * get_file_at_index( unsigned int index, struct sc_folder *first_folder )
{

	struct sc_folder *CurrFolder = first_folder;
	struct sc_file *CurrFile = first_folder->first_file;

	bool FoundIt = false;

	while (!FoundIt) {
		if (CurrFile->Index == index) {
			return CurrFile;
		}

		else {
			CurrFile = CurrFile->next;
			if (CurrFile == NULL) {
				CurrFolder = CurrFolder->next;
				if (CurrFolder == NULL)
					return NULL;

				CurrFile = CurrFolder->first_file;
			}
		}
	}
	return NULL;
}

struct sc_folder * load_file_structure( char *base_folder_path,
                                        unsigned int *total_num ) {

	struct sc_folder *prevFold = NULL;
	struct sc_file *prevFile = NULL;

	struct dirent **dirList, **fileList;
	int n, m, o, p;

	struct sc_folder *FirstFolder = NULL;
	struct sc_file *FirstFile = NULL;

	struct sc_file* new_file;
	struct sc_folder* new_folder;

	char tempName[257];
	unsigned int FilesCount = 0;
	
	*total_num = 0;
	
	printf("indexing %s\n", base_folder_path);

	n = scandir(base_folder_path, &dirList, 0, alphasort);
	if (n <= 0){
		return NULL;
	}
	for (o = 0; o < n; o++) {
		if (dirList[o]->d_name[0] != '.') {

			sprintf(tempName, "%s%s", base_folder_path, dirList[o]->d_name);

			m = scandir(tempName, &fileList, 0, alphasort);

			FirstFile = NULL;
			prevFile = NULL;

			for (p = 0; p < m; p++) { 
				if (fileList[p]->d_name[0] != '.' && strstr(fileList[p]->d_name, ".cue") == NULL) {

					new_file = (struct sc_file*) malloc(sizeof(struct sc_file));
					snprintf(new_file->full_path, 256, "%s/%s", tempName,
                        fileList[p]->d_name);

					new_file->Index = FilesCount++;

					// set up prev connection
					new_file->prev = prevFile;

					prevFile = new_file;

					// next connection (NULL FOR NOW)
					new_file->next = NULL;

					// and prev's next connection (points to this object)
					if (new_file->prev != NULL)
						new_file->prev->next = new_file;

					if (FirstFile == NULL)
						FirstFile = new_file;

					/*printf("Added file %d - %s\n", new_file->Index,
							new_file->FullPath);*/

				}
				free(fileList[p]);
			}

			if (FirstFile != NULL) { // i.e. we found at least one file

				new_folder = (struct sc_folder*) malloc(sizeof(struct sc_folder));
				sprintf(new_folder->full_path, "%s%s", base_folder_path,
                    dirList[o]->d_name);

				// set up prev connection
				new_folder->prev = prevFold;

				prevFold = new_folder;

				// next connection (NULL FOR NOW)
				new_folder->next = NULL;

				// and prev's next connection (points to this object)
				if (new_folder->prev != NULL)
					new_folder->prev->next = new_folder;

				if (FirstFolder == NULL) {
					FirstFolder = new_folder;
				}

				new_folder->first_file = FirstFile;

				//printf("Added Subfolder %s\n", new_folder->FullPath);

			}

			free(fileList);

		}

		free(dirList[o]);

	}

	free(dirList);

	*total_num = FilesCount;
	
	printf ("Added folder %s : %d files found\n", base_folder_path, FilesCount);

	return FirstFolder;

}

void dump_file_structure( struct sc_folder *FirstFolder ) {

	struct sc_folder *currFold = FirstFolder;
	struct sc_file *currFile;

	do {
		currFile = currFold->first_file;

		if (currFile != NULL) {

			do {
				printf("%s - %s\n", currFold->full_path, currFile->full_path);

			} while ((currFile = currFile->next) != NULL);
		}

	} while ((currFold = currFold->next) != NULL);

}
