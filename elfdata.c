/* Combine stripped files with separate symbols and debug information.
   Copyright (C) 2007-2015 Red Hat, Inc.
   This file is part of Red Hat elfutils.
   Written by Roland McGrath <roland@redhat.com>, 2007.
              Kushal Das <kushaldas@gmail.com> 2012

   Red Hat elfutils is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by the
   Free Software Foundation; version 2 of the License.

   Red Hat elfutils is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with Red Hat elfutils; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301 USA.

   Red Hat elfutils is an included package of the Open Invention Network.
   An included package of the Open Invention Network is a package for which
   Open Invention Network licensees cross-license their patents.  No patent
   license is granted, either expressly or impliedly, by designation as an
   included package.  Should you wish to participate in the Open Invention
   Network licensing program, please visit www.openinventionnetwork.com
   <http://www.openinventionnetwork.com>.  */


#include <Python.h>
#include <assert.h>
#include <inttypes.h>
#include <err.h>
#include <errno.h>
#include <libelf.h>
#include <libdwelf.h>
#include <stdio.h>
#include <error.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>



static PyObject*
elfdata_parseelf(PyObject *self, PyObject *args)
{
  const char* filename = NULL;
  PyObject *data = NULL;
  char magic[80];
  memset (magic , '\0', 80);
  char buf[4];

  if (!PyArg_ParseTuple(args, "s", &filename))
    Py_RETURN_NONE;

  elf_version (EV_CURRENT);
  int fd = open (filename, O_RDONLY);
  if (fd < 0)
    Py_RETURN_NONE;

  Elf *elf = elf_begin (fd, ELF_C_READ, NULL);
  if (elf == NULL)
    Py_RETURN_NONE;
  const void *build_id;
  ssize_t len = dwelf_elf_gnu_build_id (elf, &build_id);
  const unsigned char *p = build_id;
  const unsigned char *end = p + len;
  switch (len)
    {
    case 0:
    case -1:
        Py_RETURN_NONE;
    default:
      while (p < end) {
          memset(buf, '\0', 4);
          sprintf (buf, "%02" PRIx8, *p++);
          strcat(magic, buf);
        }
    }
  data = PyString_FromString(magic);
  elf_end (elf);
  close (fd);

  return data;
}

static PyMethodDef ElfDataMethods[] = {
    {"get_buildid",  elfdata_parseelf, METH_VARARGS,
     "gets GNU_BUILD_ID of the given ELF."},
    /*{"pythoncall",  kabireport_pythoncall, METH_VARARGS,
     "pass the python functions here"},*/
    {NULL, NULL, 0, NULL}        /* Sentinel */
};


PyMODINIT_FUNC
initelfdata(void)
{
        (void) Py_InitModule("elfdata", ElfDataMethods);
}


