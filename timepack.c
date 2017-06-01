#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_KEYPOINTS 1024
//#define DO_RANGES

static void usage(const char *name) {
	printf("Usage: %s infile outfile\n", name);
}

typedef struct {
	int time;
	float columns[1];
} Keypoint;

static struct {
	int columns, pattern_size;
	float *column_ranges;
	int keypoint_size;
	char *keypoints_storage;
	int rows;
} data;

int timelineRead(FILE *f) {
	if (2 != fscanf(f, "%d %d", &data.columns, &data.pattern_size)) {
		printf("Cannot read columns number and pattern size\n");
		return 0;
	}

	if (data.columns < 1) {
		printf("column count %d is not valid\n", data.columns);
		return 0;
	}

	if (data.pattern_size < 1) {
		printf("pattern size %d is not valid\n", data.pattern_size);
		return 0;
	}

	printf("Input declares %d columns\n", data.columns);

	data.column_ranges = malloc(data.columns * sizeof(float));

	for (int i = 0; i < data.columns; ++i) {
#ifdef DO_RANGES
		data.column_ranges[i] = 0;
#else
		data.column_ranges[i] = 63.5f;
#endif
	}

	data.keypoint_size = sizeof(Keypoint) + sizeof(float) * (data.columns - 1);
	data.keypoints_storage = malloc(data.keypoint_size * MAX_KEYPOINTS);

	int k = 0;
	while (!feof(f)) {
		if (k >= MAX_KEYPOINTS) {
			printf("Too many keypoints, max is %d", MAX_KEYPOINTS);
			return 0;
		}

		int pat, tick, grain;
		const int rd = fscanf(f, "%d:%d.%d", &pat, &tick, &grain);
		if (rd == -1)
			break;
		if (3 != rd) {
			printf("Row %d: Cannot read time spec\n", k + 1);
			return 0;
		}

		Keypoint *keypoint = (Keypoint*)(data.keypoints_storage + data.keypoint_size * k);
		keypoint->time = ((pat * data.pattern_size + tick) << 1) + grain;

		for (int i = 0; i < data.columns; ++i) {
			if (1 != fscanf(f, "%f", keypoint->columns + i)) {
				printf("Cannot read column %d/%d of row %d\n", i + 1, data.columns, k);
				return 0;
			}

#ifdef DO_RANGES
			const float absval = fabs(keypoint->columns[i]);
			if (data.column_ranges[i] < absval)
				data.column_ranges[i] = absval;
#endif

			if (keypoint->columns[i] < -data.column_ranges[i] || keypoint->columns[i] > data.column_ranges[i]) {
				printf("Column %d/%d row %d value %f is outside [%f, %f]\n",
						i + 1, data.columns, k, keypoint->columns[i], -data.column_ranges[i], data.column_ranges[i]);
				return 0;
			}
		}

		++k;
	}

	data.rows = k;

	for (int i = 0; i < data.columns; ++i) {
		printf("column %d range: [%f %f]\n", i, -data.column_ranges[i], data.column_ranges[i]);
	}

	return 1;
}

int timelineWrite(FILE *f) {
	fprintf(f, "/* AUTOGENERATED BY " __FILE__ ", DO NOT MODIFY */\n");
	fprintf(f, "#define TIMELINE_COLS (%d)\n", data.columns);
	fprintf(f, "#define TIMELINE_ROWS (%d)\n", data.rows);
	fprintf(f, "#pragma data_seg(\".timeline_data\")\n");
#ifdef DO_RANGES
	fprintf(f, "static const unsigned char timeline_ranges[%d] = {\n\t", data.columns);
	for (int i = 0; i < data.columns; ++i) {
		const float r = data.column_ranges[i];
		if (r > 255.f) {
			printf("Error at col %d: range is too big: %f > 255", i + 1, r);
			return 0;
		}
		data.column_ranges[i] = ceilf(r);
		fprintf(f, "%d%s", (int)(data.column_ranges[i]), (i != (data.columns-1)) ? ", " : "};\n");
	}
#endif
	fprintf(f, "static const unsigned char timeline_times[%d] = {\n\t", data.rows);

	int prevtime = 0;
	for (int i = 0; i < data.rows; ++i) {
		const Keypoint *keypoint = (Keypoint*)(data.keypoints_storage + data.keypoint_size * i);
		const int deltatime = keypoint->time - prevtime;
		if (deltatime > 255) {
			printf("Error at row %d: dt=%d, but it should be less than %d (tick/4)", i + 1, deltatime, 255);
			return 0;
		}
		if (deltatime < 0) {
			printf("Error at row %d: dt=%d, but it should be at least %d (tick/4)", i + 1, deltatime, 0);
			return 0;
		}
		fprintf(f, "%d%s", deltatime, (i != (data.rows - 1)) ? ", " : "\n};\n");
		prevtime = keypoint->time;
	}

	fprintf(f, "static const unsigned char timeline_values[%d] = {\n\t", data.columns * data.rows);
	for (int j = 0; j < data.columns; ++j) {
		for (int i = 0; i < data.rows; ++i) {
			const Keypoint *keypoint = (Keypoint*)(data.keypoints_storage + data.keypoint_size * i);
			const float r = data.column_ranges[j];
			int value = (keypoint->columns[j] + r) * 255.f / (2.f * r);
			if (value < 0) { printf("WARN: %d:%d %f -> %d\n", i, j, keypoint->columns[j], value); value = 0; }
			if (value > 255) { printf("WARN: %d:%d %f -> %d\n", i, j, keypoint->columns[j], value); value = 255; }
			fprintf(f, "%d%s", value, (i != (data.rows -1)) ? ", " : "");
		}

		fprintf(f, "%s", (j != (data.columns - 1)) ? ",\n\t" : "\n};\n");
	}
	return 1;
}

int main(int argc, char *argv[]) {
	FILE *fin, *fout;

	if (argc < 3) {
		usage(argv[0]);
		return 1;
	}

	fin = fopen(argv[1], "r");
	if (!fin) {
		printf("cannot open file %s\n", argv[1]);
		return 1;
	}

	if (!timelineRead(fin)) {
		printf("Unable to parse file %s\n", argv[1]);
		return 1;
	}

	fclose(fin);

	fout = fopen(argv[2], "w");
	if (!fin) {
		printf("cannot open file %s\n", argv[2]);
		return 1;
	}

	if (!timelineWrite(fout)) {
		printf("Unable to write file %s\n", argv[2]);
		return 1;
	}

	fclose(fout);

	return 0;
}
