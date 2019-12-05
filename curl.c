/*
 * Copyright 2019 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>
#include "em100.h"

/* For higher availability these binaries are hosted on
 * Google Drive for convenience. You can create them by yourself
 * from the installer tar ball.
 *
 * TODO: some sort of MD5 check / update check.
 */
const char *firmware_id = "1UmzGZbRkF9duwTLPi467EyfIZ6EhnMKA";
const char *firmware_name = "firmware.tar.xz";

const char *configs_id = "19jT6kNYV1TE6WNx6lUkgH0TYyKbxXcd4";
const char *configs_name = "configs.tar.xz";

const char *version_id = "1YC755W_c4nRN4qVgosegFrvfyWllqb0b";
const char *version_name = "VERSION";

#define TIMEOPT CURLINFO_TOTAL_TIME_T
#define MINIMAL_PROGRESS_FUNCTIONALITY_INTERVAL     3000000

static int xferinfo(void *p __unused,
		    curl_off_t dltotal __unused, curl_off_t dlnow __unused,
		    curl_off_t ultotal __unused, curl_off_t ulnow __unused)
{
	/* Google Drive API transfers no Content-Length, so
	 * instead of bloating this with Range: hacks, let's
	 * just print a spinning wheel.
	 */
	static int pos = 0;
	char cursor[4] = { '/', '-', '\\', '|' };
	printf("%c\b", cursor[pos]);
	fflush(stdout);
	pos = (pos + 1) % 4;
	return 0;
}

static size_t write_data(void *ptr, size_t size, size_t nmemb,
			 void *stream)
{
	return fwrite(ptr, size, nmemb, (FILE *) stream);
}

static int curl_get(const char *id, const char *filename, int progress)
{
	FILE *file;
	CURLcode res;
#define URL_BUFFER_SIZE 1024
	char url[URL_BUFFER_SIZE] =
	    "https://drive.google.com/uc?export=download&id=";

	file = fopen(filename, "wb");
	if (!file) {
		perror(filename);
		return -1;
	}

	CURL *curl = curl_easy_init();
	if (!curl) {
		printf("CURL failed.\n");
		fclose(file);
		return -1;
	}

	strncat(url, id, URL_BUFFER_SIZE - 1);

	/* Set URL to GET here */
	curl_easy_setopt(curl, CURLOPT_URL, url);
#ifdef DEBUG
	/* Switch on protocol/debug output for testing */
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
#endif

	/* Some servers don't like requests without a user-agent field. */
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "em100-agent/1.0");

	/* Write callback to write the data to disk  */
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);

	/* Write data to this file handle */
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);

	if (progress) {
		/* Simple progress indicator function */
		curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferinfo);

		/* Enable progress indicator */
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	}

	/* Follow redirections (as used by Google Drive) */
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION , 1L);

	/* Fetch the file */
	res = curl_easy_perform(curl);

	/* Close file */
	fclose(file);

	/* Clean up */
	curl_easy_cleanup(curl);

	return 0;
}

void download(const char *name, const char *id)
{
	char *filename = get_em100_file(name);
	printf("Downloading %s: ", name);
	if (curl_get(id, filename, 1))
		printf("FAILED.\n");
	else
		printf("OK\n");
	free(filename);
}

int update_all_files(void)
{
	long old_time = 0, new_time = 0;
	char old_version[256] = "<unknown>", new_version[256] = "<unknown>";

	/* Read existing version and timestamp */
	char *my_version_name = get_em100_file(version_name);
	FILE *old = fopen(my_version_name, "r");
	if (old) {
		if (fscanf(old, "Time: %ld\nVersion: %255s\n",
				        &old_time, old_version) != 2)
			printf("Parse error in %s.\n", my_version_name);
		fclose(old);
	}

	free(my_version_name);

	/* Read upstream version and timestamp */
	char *tmp_version = get_em100_file(".VERSION.new");
	if (curl_get(version_id, tmp_version, 0)) {
		printf("FAILED.\n");
	}
	FILE *new = fopen(tmp_version, "r");
	if (!new) {
		free(tmp_version);
		return 1;
	}
	if (fscanf(new, "Time: %ld\nVersion: %255s\n",
				&new_time, new_version) != 2)
		printf("Parse error in upstream VERSION.\n");
	fclose(new);
	unlink(tmp_version);
	free(tmp_version);

	/* Compare time stamps and bail out if we have the latest version */
	if (old_time >= new_time) {
		printf("Current version: %s. No newer version available.\n", old_version);
		return 0;
	}

	/* Download everything */
	if (old_time == 0)
		printf("Downloading latest version: %s\n", new_version);
	else
		printf("Update available: %s (installed: %s)\n", new_version, old_version);
	download(version_name, version_id);
	download(configs_name, configs_id);
	download(firmware_name, firmware_id);

	return 0;
}
