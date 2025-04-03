#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>

// Maximum string length
#define LENGTH 32
#define LENGTHPLUS 34

// Maximum layer types
#define NUM_LAYERS 12

typedef struct {
	int id;
	int type;
	int offset;
	int size;
	int plus;
	int flags;
	int alignment;
	char name[LENGTH];
	char filename[LENGTH];
	int checksum;
	char* data;
	bool found;
	int ahdr_tell;
} asset;

typedef struct {
	int layer_type;
	int num_assets;
	int layer_size;
	asset** assets;
} layer;

// Thanks reddit
uint32_t reverse_endian_32(uint32_t number) {
	return ((number & 0xFF) << 24) | ((number & 0xFF00) << 8) | ((number & 0xFF0000) >> 8) | (number >> 24);
}

int read_flipped_int(FILE* file) {
	int read;
	fread(&read, sizeof(int), 1, file);
	read = reverse_endian_32(read);
	return read;
}

void write_flipped_int(int n, FILE* file) {
	n = reverse_endian_32(n);
	fwrite(&n, sizeof(int), 1, file);
}

void read_string(char* buffer, FILE* file) {
	int i = 0;
	int j = 0;
	for (; i < LENGTH; i++) {
		char c;
		fread(&c, sizeof(char), 1, file);
		if (c == '\0') {
			if (i > 0) {
				break;
			}
		}
		else {
			buffer[j] = c;
			j++;
		}
	}
	if (j < LENGTH) {
		for (; j < LENGTH; j++) {
			buffer[j] = '\0';
		}
	}
}

void get_output_string(char* str, char* out, int* out_length) {
	int i = 0;
	int in_length = strlen(str);
	int len = 1;

	for (; i < in_length; i++) {
		char c = str[i];
		out[i] = c;

		if (c == '\0') {
			break;
		}

		len++;
	}

	out[len] = '\0';
	if (len % 2 != 0) {
		len++;
		out[len] = '\0';
	}

	*out_length = len;
}

int compare_blocks(FILE* file1, FILE* file2, int size) {
	int ret = 0;
	char* block1 = malloc(size);
	char* block2 = malloc(size);

	if (block1 == NULL) {
		free(block1);
		return -1;
	}

	if (block2 == NULL) {
		free(block2);
		return -1;
	}

	fread(block1, sizeof(char), size, file1);
	fread(block2, sizeof(char), size, file2);

	for (int i = 0; i < size; i++) {
		if (block1[i] != block2[i]) {
			ret++;
			break;
		}
	}

	free(block1);
	free(block2);
	return ret;
}

asset* read_asset(FILE* file) {
	asset* a = malloc(sizeof(asset));
	if (a != NULL) {
		fseek(file, 0x8, SEEK_CUR);

		a->id = read_flipped_int(file);
		a->type = read_flipped_int(file);
		a->offset = read_flipped_int(file);
		a->size = read_flipped_int(file);
		a->plus = read_flipped_int(file);
		a->flags = read_flipped_int(file);

		fseek(file, 0x4, SEEK_CUR);
		int off = read_flipped_int(file) + ftell(file) - 4;

		a->alignment = read_flipped_int(file);
		read_string(a->name, file);

		fseek(file, off, SEEK_SET);
		a->checksum = read_flipped_int(file);
		a->data = NULL;
		a->found = false;
	}
	return a;
}

asset* make_dummy(int id) {
	asset* a = malloc(sizeof(asset));
	if (a != NULL) {
		a->id = id;
		a->type = 0x54455854; // TEXT
		a->offset = 0x0;
		a->size = 0x8;
		a->plus = 0x0;
		a->flags = 0x0;
		a->alignment = 0x0;
		strncpy(a->name, "dummy", 6);
		a->checksum = 0x0;
		a->data = calloc(8, sizeof(char));
		a->found = false;
	}
	return a;
}

int get_padding(int size, int alignment) {
	return alignment * ((int)(size / alignment) + 1) - size;
}

int layer_indices_bfbb[9] = { 1, 2, 10, 3, 4, 0, 8, 6, 7 };
int layer_indices[11] = { 1, 2, 3, 11, 4, 5, 0, 9, 7, 8, 10 };

int layer_type_to_index(int type, bool bfbb) {
	if (bfbb == true) {
		for (int i = 0; i < 9; i++) {
			if (layer_indices_bfbb[i] == type) {
				return i;
			}
		}
		return 0;
	}
	else {
		for (int i = 0; i < 11; i++) {
			if (layer_indices[i] == type) {
				return i;
			}
		}
		return 0;
	}
}

int index_to_layer_type(int type, bool bfbb) {
	if (bfbb == true) {
		return layer_indices_bfbb[type];
	}
	else {
		return layer_indices[type];
	}
}

asset** input_assets;
asset** output_assets;
FILE* original_file;
FILE* modified_file;
FILE* output_file;
layer** output_layers;
bool is_bfbb;

int main(int argc, char* argv[]) {
	int ret = 0;
	int original_num_assets = 0;
	int modified_num_assets = 0;
	int output_num_assets = 0;
	int original_num_layers = 0;
	int modified_num_layers = 0;
	int output_num_layers = 0;
	int layer_alignment = 32;
	int asset_alignment = 16;
	is_bfbb = false;

	if (argc < 3) {
		fprintf(stderr, "Usage: HIPDelta original.hip modified.hip [output.hip]\n");
		ret = 1;
		goto exit;
	}

	if (argc < 4) {
		argv[3] = "output.hip";
	}

	original_file = fopen(argv[1], "rb");

	if (original_file == NULL) {
		fprintf(stderr, "Could not open file %s for reading\n", argv[1]);
		ret = 1;
		goto exit;
	}

	modified_file = fopen(argv[2], "rb");

	if (modified_file == NULL) {
		fprintf(stderr, "Could not open file %s for reading\n", argv[2]);
		ret = 1;
		goto exit;
	}

	output_file = fopen(argv[3], "wb");

	if (output_file == NULL) {
		fprintf(stderr, "Could not open file %s for writing\n", argv[3]);
		ret = 1;
		goto exit;
	}

	if (read_flipped_int(original_file) != 0x48495041) { // HIPA
		fprintf(stderr, "%s is not a HIP file\n", argv[1]);
		ret = 1;
		goto exit;
	}

	if (read_flipped_int(modified_file) != 0x48495041) { // HIPA
		fprintf(stderr, "%s is not a HIP file\n", argv[2]);
		ret = 1;
		goto exit;
	}

	// Get asset counts

	fseek(original_file, 0x38, SEEK_SET);
	original_num_assets = read_flipped_int(original_file);
	original_num_layers = read_flipped_int(original_file);

	fseek(modified_file, 0x38, SEEK_SET);
	modified_num_assets = read_flipped_int(modified_file);
	modified_num_layers = read_flipped_int(modified_file);

	fprintf(stdout, "%d asset(s) in original file, %d asset(s) in modified file\n", original_num_assets, modified_num_assets);

	// Seek to PLAT block

	fseek(original_file, 0x50, SEEK_SET); // PCRT length
	fseek(original_file, read_flipped_int(original_file) + 16, SEEK_CUR); // platform ID

	fseek(modified_file, 0x50, SEEK_SET); // PCRT length
	fseek(modified_file, read_flipped_int(modified_file) + 16, SEEK_CUR); // platform ID
	int plat_tell = ftell(modified_file) + 4;

	// Check if PLAT blocks match

	int plat_size = read_flipped_int(original_file);
	if (read_flipped_int(modified_file) == plat_size) {
		if (compare_blocks(original_file, modified_file, plat_size, true) != 0) {
			fprintf(stderr, "Platform info does not match\n");
			ret = 1;
			goto exit;
		}
	}
	else {
		fprintf(stderr, "Platform info does not match\n");
		ret = 1;
		goto exit;
	}

	// Add all assets in original file to input array

	input_assets = malloc(sizeof(asset*) * original_num_assets);

	if (input_assets == NULL) {
		ret = 1;
		goto exit;
	}

	fseek(original_file, 0x1C, SEEK_CUR);
	for (int i = 0; i < original_num_assets; i++) {
		asset* a = read_asset(original_file);
		if (a == NULL) {
			ret = 1;
			goto exit;
		}
		input_assets[i] = a;
	}
	int original_tell = ftell(original_file);

	// Add every differing asset from modified file to output array
	fprintf(stdout, "\nCreating new HIP with these assets:\n");

	output_assets = calloc(max(original_num_assets, modified_num_assets), sizeof(asset*));

	if (output_assets == NULL) {
		ret = 1;
		goto exit;
	}

	fseek(modified_file, 0x1C, SEEK_CUR);
	for (int i = 0; i < modified_num_assets; i++) {
		asset* a = read_asset(modified_file);
		if (a == NULL) {
			ret = 1;
			goto exit;
		}

		int tell = ftell(modified_file);
		bool output = true;
		for (int j = 0; j < original_num_assets; j++) {
			asset* a2 = input_assets[j];

			if (a->id == a2->id) {
				a2->found = true;
				if (a->size == a2->size) {
					fseek(original_file, a2->offset, SEEK_SET);
					fseek(modified_file, a->offset, SEEK_SET);

					if (compare_blocks(original_file, modified_file, a->size) == 0) {
						output = false;
					}
				}
				break;
			}
		}
		fseek(modified_file, tell, SEEK_SET);

		if (output == true) {
			output_assets[output_num_assets] = a;
			output_num_assets++;
		}
	}

	int tell = ftell(modified_file);
	fseek(modified_file, plat_tell, SEEK_SET);
	fseek(modified_file, 0x4, SEEK_CUR);
	char pl[2] = { 0 };
	fread(pl, sizeof(char), 2, modified_file);
	if (pl[0] == 'G' && pl[1] == 'a') {
		is_bfbb = true;
	}
	fseek(modified_file, tell, SEEK_SET);

	// Put assets into layers

	output_layers = calloc(NUM_LAYERS, sizeof(layer*));
	for (int i = 0; i < NUM_LAYERS; i++) {
		layer* l = malloc(sizeof(layer));
		if (l == NULL) {
			ret = 1;
			goto exit;
		}

		l->layer_type = index_to_layer_type(i, is_bfbb);
		l->num_assets = 0;
		l->assets = calloc(max(original_num_assets, modified_num_assets), sizeof(asset*));

		output_layers[i] = l;
	}

	fseek(modified_file, 0x14, SEEK_CUR);

	for (int i = 0; i < modified_num_layers; i++) {
		fseek(modified_file, 0x8, SEEK_CUR);
		int type = layer_type_to_index(read_flipped_int(modified_file), is_bfbb);
		int count = read_flipped_int(modified_file);
		if (count > 0) {
			for (int j = 0; j < count; j++) {
				int id = read_flipped_int(modified_file);
				for (int k = 0; k < output_num_assets; k++) {
					asset* a = output_assets[k];
					if (a->id == id) {
						if (output_layers[type]->num_assets == 0) {
							output_num_layers++;
							output_layers[type]->layer_size = 0;
						}

						output_layers[type]->assets[output_layers[type]->num_assets] = a;
						output_layers[type]->num_assets++;
						a->plus = get_padding(a->size, asset_alignment);
						output_layers[type]->layer_size += a->size + a->plus;

						fprintf(stdout, "    Layer %d: [%08x] %s\n", type, a->id, a->name);
						break;
					}
				}
			}
		}
		fseek(modified_file, 0xC, SEEK_CUR);
	}

	fseek(original_file, original_tell, SEEK_SET);
	fseek(original_file, 0x14, SEEK_CUR);
	fprintf(stdout, "\n");

	int num_dummy = 0;

	for (int i = 0; i < original_num_layers; i++) {
		fseek(original_file, 0x8, SEEK_CUR);
		int type = layer_type_to_index(read_flipped_int(original_file), is_bfbb);
		int count = read_flipped_int(original_file);
		if (count > 0) {
			for (int j = 0; j < count; j++) {
				int id = read_flipped_int(original_file);
				for (int k = 0; k < original_num_assets; k++) {
					asset* a = input_assets[k];
					if (a->id == id) {
						if (a->found == false) {
							if (output_layers[type]->num_assets == 0) {
								output_num_layers++;
								output_layers[type]->layer_size = 0;
							}

							asset* dummy = make_dummy(a->id);
							output_layers[type]->assets[output_layers[type]->num_assets] = dummy;
							output_layers[type]->num_assets++;
							dummy->plus = get_padding(dummy->size, asset_alignment);
							output_layers[type]->layer_size += dummy->size + dummy->plus;

							num_dummy++;
							fprintf(stdout, "Dummying asset [%08x] %s\n", a->id, a->name);
						}
						break;
					}
				}
			}
		}
		fseek(original_file, 0xC, SEEK_CUR);
	}

	// Read data block for each asset to be outputted

	for (int i = 0; i < output_num_assets; i++) {
		asset* a = output_assets[i];
		a->data = calloc(a->size, sizeof(char));
		if (a->data == NULL) {
			ret = 1;
			goto exit;
		}

		fseek(modified_file, a->offset, SEEK_SET);
		fread(a->data, sizeof(char), a->size, modified_file);
	}

	// Copy HIPA, PACK, PVER, PFLG from modified file
	char p = 0x33;

	{
		fseek(modified_file, 0x0, SEEK_SET);

		char* buf = malloc(sizeof(char) * 0x30);
		if (buf == NULL) {
			ret = 1;
			goto close;
		}
		fread(buf, sizeof(char), 0x30, modified_file);
		fwrite(buf, sizeof(char), 0x30, output_file);
		free(buf);
	}
	
	// Write PCNT block
	{
		int pcnt[6] = {
			reverse_endian_32(20), // length
			reverse_endian_32(output_num_assets + num_dummy),
			reverse_endian_32(output_num_layers),
			0, 0, 0 // unused
		};

		fwrite("PCNT", sizeof(char), 4, output_file);
		fwrite(&pcnt, sizeof(int), 6, output_file);
	}

	// Write PCRT, PMOD block
	{
		time_t t = time(0);
		char* str[LENGTHPLUS] = { 0 };
		int len;
		get_output_string(ctime(&t), str, &len);

		int pcrt[2] = {
			reverse_endian_32(4 + len),
			reverse_endian_32(t)
		};

		fwrite("PCRT", sizeof(char), 4, output_file);
		fwrite(pcrt, sizeof(int), 2, output_file);
		fwrite(str, sizeof(char), len, output_file);

		fwrite("PMOD", sizeof(char), 4, output_file);
		write_flipped_int(4, output_file);
		fwrite(&pcrt[1], sizeof(int), 1, output_file);
	}

	// Copy PLAT from modified file
	{
		fwrite("PLAT", sizeof(char), 4, output_file);
		write_flipped_int(plat_size, output_file);

		fseek(modified_file, plat_tell, SEEK_SET);

		char* buf = malloc(sizeof(char) * plat_size);
		if (buf == NULL) {
			ret = 1;
			goto close;
		}
		fread(buf, sizeof(char), plat_size, modified_file);
		fwrite(buf, sizeof(char), plat_size, output_file);

		free(buf);
	}

	// Write DICT block
	char ainf[12] = {
		'A', 'I', 'N', 'F', 0x0, 0x0, 0x0, 0x4, 0x0, 0x0, 0x0, 0x0
	};

	char ldbg[12] = {
		'L', 'D', 'B', 'G', 0X0, 0X0, 0X0, 0X4, 0xFF, 0xFF, 0xFF, 0xFF
	};


	int atoc_size = 0;
	int ltoc_size = 0;
	int dpak_size = 0;

	fwrite("DICT", sizeof(char), 4, output_file);

	int dict_size_tell = ftell(output_file);
	write_flipped_int(0, output_file);
	fwrite("ATOC", sizeof(char), 4, output_file);

	int atoc_size_tell = ftell(output_file);
	write_flipped_int(0, output_file);
	fwrite(ainf, sizeof(char), 12, output_file);
	atoc_size += 12;

	for (int i = 0; i < NUM_LAYERS; i++) {
		layer* l = output_layers[i];
		if (l->num_assets == 0) {
			continue;
		}

		for (int j = 0; j < l->num_assets; j++) {
			asset* a = l->assets[j];

			char* name[LENGTHPLUS] = { 0 };
			int name_len;
			get_output_string(a->name, name, &name_len);

			char filename[2] = { 0x0, 0x0 };

			fwrite("AHDR", sizeof(char), 4, output_file);
			write_flipped_int(40 + name_len + 2, output_file);
			write_flipped_int(a->id, output_file);
			write_flipped_int(a->type, output_file);

			a->ahdr_tell = ftell(output_file);
			write_flipped_int(0, output_file); // placeholder - offset
			write_flipped_int(a->size, output_file);
			write_flipped_int(0, output_file); // placeholder - plus
			write_flipped_int(a->flags, output_file);

			fwrite("ADBG", sizeof(char), 4, output_file);
			write_flipped_int(8 + name_len + 2, output_file);
			write_flipped_int(a->alignment, output_file);
			fwrite(name, sizeof(char), name_len, output_file);
			fwrite(filename, sizeof(char), 2, output_file);

			write_flipped_int(a->checksum, output_file);

			atoc_size += 48 + name_len + 2;
		}
	}

	ainf[0] = 'L';
	fwrite("LTOC", sizeof(char), 4, output_file);

	int ltoc_size_tell = ftell(output_file);
	write_flipped_int(0, output_file);
	fwrite(ainf, sizeof(char), 12, output_file);
	ltoc_size += 12;

	for (int i = 0; i < NUM_LAYERS; i++) {
		layer* l = output_layers[i];
		if (l->num_assets == 0) {
			continue;
		}

		fwrite("LHDR", sizeof(char), 4, output_file);
		write_flipped_int((4 * l->num_assets) + 8, output_file);
		write_flipped_int(l->layer_type, output_file);
		write_flipped_int(l->num_assets, output_file);

		for (int j = 0; j < l->num_assets; j++) {
			asset* a = l->assets[j];
			write_flipped_int(a->id, output_file);
			ltoc_size += 4;
		}

		fwrite(ldbg, sizeof(char), 12, output_file);

		ltoc_size += 28;
	}

	// Write STRM block
	ldbg[0] = 'D';
	ldbg[1] = 'H';
	ldbg[2] = 'D';
	ldbg[3] = 'R';

	fwrite("STRM", sizeof(char), 4, output_file);

	int strm_size_tell = ftell(output_file);
	write_flipped_int(0, output_file);
	fwrite(ldbg, sizeof(char), 12, output_file);
	fwrite("DPAK", sizeof(char), 4, output_file);

	int dpak_size_tell = ftell(output_file);
	write_flipped_int(0, output_file);

	int pad = get_padding(ftell(output_file)+4, layer_alignment) + 4;
	write_flipped_int(pad, output_file);
	dpak_size += pad;

	for (int i = 0; i < pad-4; i++) {
		fwrite(&p, sizeof(char), 1, output_file);
	}

	for (int i = 0; i < NUM_LAYERS; i++) {
		layer* l = output_layers[i];

		if (l->num_assets == 0) {
			continue;
		}

		for (int j = 0; j < l->num_assets; j++) {
			int pos = ftell(output_file);
			asset* a = l->assets[j];
			fwrite(a->data, sizeof(char), a->size, output_file);

			int pad = get_padding(ftell(output_file), asset_alignment);
			for (int k = 0; k < pad; k++) {
				fwrite(&p, sizeof(char), 1, output_file);
			}

			int pos2 = ftell(output_file);

			fseek(output_file, a->ahdr_tell, SEEK_SET);
			write_flipped_int(pos, output_file);
			fseek(output_file, 0x4, SEEK_CUR);
			write_flipped_int(pad, output_file);
			fseek(output_file, pos2, SEEK_SET);

			dpak_size += a->size + pad;
		}
	}

	fseek(output_file, dict_size_tell, SEEK_SET);
	write_flipped_int(atoc_size + ltoc_size + 16, output_file);
	fseek(output_file, atoc_size_tell, SEEK_SET);
	write_flipped_int(atoc_size, output_file);
	fseek(output_file, ltoc_size_tell, SEEK_SET);
	write_flipped_int(ltoc_size, output_file);
	fseek(output_file, strm_size_tell, SEEK_SET);
	write_flipped_int(dpak_size + 20, output_file);
	fseek(output_file, dpak_size_tell, SEEK_SET);
	write_flipped_int(dpak_size, output_file);

	fprintf(stdout, "\nWritten to %s\n", argv[3]);

close:
	fclose(output_file);

exit:
	for (int i = 0; i < original_num_assets; i++) {
		free(input_assets[i]);
	}
	for (int i = 0; i < output_num_assets; i++) {
		free(output_assets[i]->data);
		free(output_assets[i]);
	}
	if (output_layers != NULL) {
		for (int i = 0; i < NUM_LAYERS; i++) {
			if (output_layers[i]->num_assets != 0) {
				free(output_layers[i]->assets);
			}
			free(output_layers[i]);
		}
		free(output_layers);
	}
	free(input_assets);
	free(output_assets);
	return ret;
}