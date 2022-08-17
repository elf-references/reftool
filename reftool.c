/*
 * Copyright 2022 Chainguard, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <err.h>

#include <libelf.h>
#include <gelf.h>

const char *REFERENCE_SECTION_NAME = ".reference";

struct note {
	GElf_Word namesz;
	GElf_Word descsz;
	GElf_Word type;
	char buf[];
};

static void
elf_errx(const char *fmt, ...)
{
	va_list va;
	char buf[65535];
	int err = elf_errno();

	va_start(va, fmt);
	vsnprintf(buf, sizeof buf, fmt, va);
	va_end(va);

	errx(EXIT_FAILURE, "%s: %s", buf, elf_errmsg(err));
}

static Elf *
elf_open(const char *file, Elf_Cmd cmd, int *fdout)
{
	int fd;
	int flags = 0;
	Elf *elf;

	switch (cmd)
	{
	case ELF_C_READ:
		flags |= O_RDONLY;
		break;
	case ELF_C_RDWR:
		flags |= O_RDWR;
		break;
	case ELF_C_WRITE:
		flags |= O_WRONLY;
		break;
	default:
		break;
	}

	*fdout = fd = open(file, flags);
	if (fd == -1)
	{
		err(EXIT_FAILURE, "opening file %s", file);
	}

	elf = elf_begin(fd, cmd, NULL);
	if (elf == NULL)
	{
		elf_errx("parsing file %s", file);
	}

	return elf;
}

static bool
read_section(Elf *elf, Elf_Scn *cursor)
{
	GElf_Ehdr ehdr;
	GElf_Shdr shdr;
	const char *sh_name;

	if (gelf_getehdr(elf, &ehdr) == NULL)
	{
		return false;
	}

	if (gelf_getshdr(cursor, &shdr) == NULL)
	{
		return false;
	}

	sh_name = elf_strptr(elf, ehdr.e_shstrndx, shdr.sh_name);
	if (sh_name == NULL)
	{
		return false;
	}

	if (strcmp(sh_name, REFERENCE_SECTION_NAME))
	{
		return true;
	}

	Elf_Data *data = NULL;
	for (data = elf_getdata(cursor, data); data != NULL; data = elf_getdata(cursor, data))
	{
		struct note *n = data->d_buf;

		if (n != NULL)
		{
			printf("%s (%s)\n", n->buf + n->namesz, n->buf);
		}
	}

	return true;
}

int
usage(const char *progname)
{
	fprintf(stderr, "usage: %s [command] [args...]\n\n", progname);

	fprintf(stderr, "commands:\n");
	fprintf(stderr, "   add         add references\n");
	fprintf(stderr, "   list        list references\n");

	return EXIT_FAILURE;
}

int
list(const char *file)
{
	int fd;
	Elf *elf;
	Elf_Scn *cursor = NULL;

	elf = elf_open(file, ELF_C_READ, &fd);
	if (elf == NULL)
	{
		err(EXIT_FAILURE, "elf_open(%s, ELF_C_READ)", file);
	}

	for (cursor = elf_nextscn(elf, cursor); cursor != NULL; cursor = elf_nextscn(elf, cursor))
	{
		if (!read_section(elf, cursor))
		{
			elf_errx("reading section");
		}
	}

	if (elf_end(elf) == 0)
	{
		close(fd);
	}

	return EXIT_SUCCESS;
}

/* find_or_add_strtab_ref finds or adds a strtab entry for
 * REFERENCE_SECTION_NAME. */
static GElf_Word
find_or_add_strtab_ref(Elf *elf)
{
	GElf_Ehdr ehdr;
	GElf_Shdr shdr;

	if (gelf_getehdr(elf, &ehdr) == NULL)
	{
		elf_errx("reading program header");
	}

	/* Look for a pre-existing reference.  If one exists, we don't
	 * need to edit the string table. */
	Elf_Scn *cursor = NULL;
	for (cursor = elf_nextscn(elf, cursor); cursor != NULL; cursor = elf_nextscn(elf, cursor))
	{
		const char *sh_name;

		if (gelf_getshdr(cursor, &shdr) == NULL)
		{
			elf_errx("reading section header");
		}

		sh_name = elf_strptr(elf, ehdr.e_shstrndx, shdr.sh_name);
		if (sh_name == NULL)
		{
			elf_errx("reading section name");
		}

		if (!strcmp(sh_name, REFERENCE_SECTION_NAME))
		{
			return shdr.sh_name;
		}
	}

	/* No pre-existing reference was found, so we have to extend the
	 * string table. */
	Elf_Scn *scn = elf_getscn(elf, ehdr.e_shstrndx);
	if (scn == NULL)
	{
		elf_errx("reading string table");
	}

	Elf_Data *str_data = elf_getdata(scn, NULL);
	if (str_data == NULL)
	{
		elf_errx("getting data descriptor for string table");
	}

	GElf_Word offset = str_data->d_size;
	size_t new_size = offset + strlen(REFERENCE_SECTION_NAME) + 1;

	void *new_table = calloc(1, new_size);
	if (new_table == NULL)
	{
		err(EXIT_FAILURE, "malloc");
	}

	memcpy(new_table, str_data->d_buf, str_data->d_size);
	memcpy((char *) new_table + offset, REFERENCE_SECTION_NAME, strlen(REFERENCE_SECTION_NAME) + 1);

	str_data->d_buf = new_table;
	str_data->d_size = new_size;

	elf_flagdata(str_data, ELF_C_SET, ELF_F_DIRTY);

	if (gelf_getshdr(scn, &shdr) == NULL)
	{
		elf_errx("reading section header");
	}

	shdr.sh_size = str_data->d_size;

	if (!gelf_update_shdr(scn, &shdr))
	{
		elf_errx("updating section header");
	}

	return offset;
}

int
add(const char *file, const char *mediatype, const char *data)
{
	int fd;
	Elf *elf;
	GElf_Word mtlen = strlen(mediatype);
	GElf_Word dlen = strlen(data);

	mtlen += (4 - (mtlen & 3)) & 3;
	dlen += (4 - (dlen & 3)) & 3;

	size_t bufsize = sizeof(struct note) + mtlen + dlen;
	void *buf = calloc(1, bufsize);
	if (buf == NULL)
	{
		err(EXIT_FAILURE, "malloc");
	}

	struct note *note = buf;
	note->namesz = mtlen;
	note->descsz = dlen;

	memcpy(note->buf + note->namesz, data, strlen(data));
	memcpy(note->buf, mediatype, strlen(mediatype));

	elf = elf_open(file, ELF_C_RDWR, &fd);
	if (elf == NULL)
	{
		err(EXIT_FAILURE, "elf_open(%s, ELF_C_READ)", file);
	}

	elf_flagelf(elf, ELF_C_SET, ELF_F_LAYOUT);
	elf_flagelf(elf, ELF_C_SET, ELF_F_DIRTY);

	GElf_Word sh_name = find_or_add_strtab_ref(elf);

	/* find the right offset to use. */
	GElf_Off last_offset = 0;
	Elf_Scn *cursor = NULL;

	while ((cursor = elf_nextscn(elf, cursor)) != NULL)
	{
		GElf_Shdr shdr;

		if (gelf_getshdr(cursor, &shdr) == NULL)
		{
			elf_errx("gelf_getshdr");
		}

		if (shdr.sh_type != SHT_NOBITS &&
			last_offset < shdr.sh_offset + shdr.sh_size)
		{
			last_offset = shdr.sh_offset + shdr.sh_size;
		}
	}

	Elf_Scn *ref_scn = elf_newscn(elf);
	if (ref_scn == NULL)
	{
		elf_errx("elf_newscn()");
	}

	elf_flagscn(ref_scn, ELF_C_SET, ELF_F_DIRTY);

	Elf_Data *ref_data = elf_newdata(ref_scn);
	if (ref_data == NULL)
	{
		elf_errx("elf_newdata()");
	}

	ref_data->d_off = 0;
	ref_data->d_buf = buf;
	ref_data->d_size = bufsize;
	ref_data->d_type = ELF_T_BYTE;
	ref_data->d_version = EV_CURRENT;
	elf_flagdata(ref_data, ELF_C_SET, ELF_F_DIRTY);

	GElf_Shdr ref_shdr_mem;
	GElf_Shdr *ref_shdr = gelf_getshdr(ref_scn, &ref_shdr_mem);
	if (ref_shdr == NULL)
	{
		elf_errx("gelf_getshdr");
	}

	ref_shdr->sh_entsize = 0;
	ref_shdr->sh_flags = SHF_ALLOC;
	ref_shdr->sh_name = sh_name;
	ref_shdr->sh_type = SHT_NOTE;
	ref_shdr->sh_offset = last_offset;
	ref_shdr->sh_size = bufsize;
	ref_shdr->sh_addralign = 1;

	if (!gelf_update_shdr(ref_scn, ref_shdr))
	{
		elf_errx("gelf_update_shdr");
	}

	GElf_Ehdr ehdr;

	if (gelf_getehdr(elf, &ehdr) == NULL)
	{
		elf_errx("reading program header");
	}

	ehdr.e_shoff = last_offset + bufsize;

	if (!gelf_update_ehdr(elf, &ehdr))
	{
		elf_errx("updating program header");
	}

	if (elf_update(elf, ELF_C_NULL) < 0 ||
	    elf_update(elf, ELF_C_WRITE) < 0)
	{
		elf_errx("updating ELF");
	}

	if (elf_end(elf) == 0)
	{
		close(fd);
	}

	free(buf);

	return EXIT_SUCCESS;
}

int
main(int argc, const char *argv[])
{
	(void) elf_version(EV_CURRENT);

	if (argc < 2)
		return usage(argv[0]);

	if (!strcmp(argv[1], "list"))
	{
		if (argc < 3)
			return usage(argv[0]);

		return list(argv[2]);
	}
	else if (!strcmp(argv[1], "add"))
	{
		if (argc < 5)
			return usage(argv[0]);

		return add(argv[2], argv[3], argv[4]);
	}
	else
		return usage(argv[0]);

	return EXIT_SUCCESS;
}
