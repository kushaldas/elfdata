/* Combine stripped files with separate symbols and debug information.
   Copyright (C) 2007-2011 Red Hat, Inc.
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
#include <argp.h>
#include <assert.h>
#include <fnmatch.h>
#include <libintl.h>
#include <locale.h>
#include <mcheck.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdio_ext.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <gelf.h>
#include <libebl.h>
#include <libdwfl.h>


/* Definitions of arguments for argp functions.  */
static const struct argp_option options[] =
{
  /* Group 2 will follow group 1 from dwfl_standard_argp.  */
  { "match-file-names", 'f', NULL, 0,
    "Match MODULE against file names, not module names", 2 },
  { "ignore-missing", 'i', NULL, 0, "Silently skip unfindable files", 0 },

  { NULL, 0, NULL, 0, "Output options:", 0 },
  { "output", 'o', "FILE", 0, "Place output into FILE", 0 },
  { "output-directory", 'd', "DIRECTORY",
    0, "Create multiple output files under DIRECTORY", 0 },
  { "module-names", 'm', NULL, 0, "Use module rather than file names", 0 },
  { "all", 'a', NULL, 0,
    "Create output for modules that have no separate debug information",
    0 },
  { "relocate", 'R', NULL, 0,
    "Apply relocations to section contents in ET_REL files", 0 },
  { "list-only", 'n', NULL, 0,
    "Only list module and file names, build IDs", 0 },
  { NULL, 0, NULL, 0, NULL, 0 }
};

struct arg_info
{
  const char *output_file;
  const char *output_dir;
  Dwfl *dwfl;
  char **args;
  bool list;
  bool all;
  bool ignore;
  bool modnames;
  bool match_files;
  bool relocate;
};

/* Handle program arguments.  */
static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  struct arg_info *info = state->input;

  switch (key)
    {
    case ARGP_KEY_INIT:
      state->child_inputs[0] = &info->dwfl;
      break;

    case 'o':
      if (info->output_file != NULL)
  {
    argp_error (state, "-o option specified twice");
    return EINVAL;
  }
      info->output_file = arg;
      break;

    case 'd':
      if (info->output_dir != NULL)
  {
    argp_error (state, "-d option specified twice");
    return EINVAL;
  }
      info->output_dir = arg;
      break;

    case 'm':
      info->modnames = true;
      break;
    case 'f':
      info->match_files = true;
      break;
    case 'a':
      info->all = true;
      break;
    case 'i':
      info->ignore = true;
      break;
    case 'n':
      info->list = true;
      break;
    case 'R':
      info->relocate = true;
      break;

    case ARGP_KEY_ARGS:
    case ARGP_KEY_NO_ARGS:
      /* We "consume" all the arguments here.  */
      info->args = &state->argv[state->next];

      if (info->output_file != NULL && info->output_dir != NULL)
  {
    argp_error (state, "only one of -o or -d allowed");
    return EINVAL;
  }

      if (info->list && (info->dwfl == NULL
       || info->output_dir != NULL
       || info->output_file != NULL))
  {
    argp_error (state,
          "-n cannot be used with explicit files or -o or -d");
    return EINVAL;
  }

      if (info->output_dir != NULL)
  {
    struct stat64 st;
    error_t fail = 0;
    if (stat64 (info->output_dir, &st) < 0)
      fail = errno;
    else if (!S_ISDIR (st.st_mode))
      fail = ENOTDIR;
    if (fail)
      {
        argp_failure (state, EXIT_FAILURE, fail,
          "output directory '%s'", info->output_dir);
        return fail;
      }
  }

      if (info->dwfl == NULL)
  {
    if (state->next + 2 != state->argc)
      {
        argp_error (state, "exactly two file arguments are required");
        return EINVAL;
      }

    if (info->ignore || info->all || info->modnames || info->relocate)
      {
        argp_error (state, "\
-m, -a, -R, and -i options not allowed with explicit files");
        return EINVAL;
      }

    /* Bail out immediately to prevent dwfl_standard_argp's parser
       from defaulting to "-e a.out".  */
    return ENOSYS;
  }
      else if (info->output_file == NULL && info->output_dir == NULL
         && !info->list)
  {
    argp_error (state,
          "-o or -d is required when using implicit files");
    return EINVAL;
  }
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}



PyObject *
list_module (Dwfl_Module *mod)
{

  PyObject * buildid;
  char magic[80];
  memset (magic , '\0', 80);
  char buf[4];
  /* Make sure we have searched for the files.  */
  GElf_Addr bias;
  bool have_elf = dwfl_module_getelf (mod, &bias) != NULL;
  bool have_dwarf = dwfl_module_getdwarf (mod, &bias) != NULL;

  const char *file;
  const char *debug;
  Dwarf_Addr start;
  Dwarf_Addr end;
  const char *name = dwfl_module_info (mod, NULL, &start, &end,
               NULL, NULL, &file, &debug);
  if (file != NULL && debug != NULL && (debug == file || !strcmp (debug, file)))
    debug = ".";

  const unsigned char *id;
  GElf_Addr id_vaddr;
  int id_len = dwfl_module_build_id (mod, &id, &id_vaddr);

  if (id_len > 0)
    {
      do{

          memset(buf, '\0', 4);
          sprintf (buf, "%02" PRIx8, *id++);
          strcat(magic, buf);
      }while (--id_len > 0);
    }
  buildid = PyString_FromString(magic);
  return buildid;
}


struct match_module_info
{
  char **patterns;
  Dwfl_Module *found;
  bool match_files;
};

static int
match_module (Dwfl_Module *mod,
        void **userdata __attribute__ ((unused)),
        const char *name,
        Dwarf_Addr start __attribute__ ((unused)),
        void *arg)
{
  struct match_module_info *info = arg;

  if (info->patterns[0] == NULL) /* Match all.  */
    {
    match:
      info->found = mod;
      return DWARF_CB_ABORT;
    }

  if (info->match_files)
    {
      /* Make sure we've searched for the ELF file.  */
      GElf_Addr bias;
      (void) dwfl_module_getelf (mod, &bias);

      const char *file;
      const char *check = dwfl_module_info (mod, NULL, NULL, NULL,
              NULL, NULL, &file, NULL);
      assert (check == name);
      if (file == NULL)
  return DWARF_CB_OK;

      name = file;
    }

  /*for (char **p = info->patterns; *p != NULL; ++p)
    if (fnmatch (*p, name, 0) == 0)
      goto match;*/

  char **p = info->patterns;
  if (*p && (fnmatch (*p, name, 0) == 0))
    goto match;

  return DWARF_CB_OK;
}


/* Handle files opened implicitly via libdwfl.  */
PyObject *
handle_implicit_modules (const struct arg_info *info)
{
  PyObject *ids = PyList_New(0);
  PyObject *tmp;
  struct match_module_info mmi = {info->args, NULL, true };
  inline ptrdiff_t next (ptrdiff_t offset)
    {
      return dwfl_getmodules (info->dwfl, &match_module, &mmi, offset);
    }
  ptrdiff_t offset = next (0);
  if (offset == 0)
    error (EXIT_FAILURE, 0, "no matching modules found");

  do {
      tmp = list_module (mmi.found);
      PyList_Append(ids, tmp);
  }while ((offset = next (offset)) > 0);

  return ids;
}


static PyObject*
elfdata_parseelf(PyObject *self, PyObject *args)
{
    const char* filename = NULL;
    PyObject *data = NULL;

    if (!PyArg_ParseTuple(args, "s", &filename))
                return NULL;

    elf_version (EV_CURRENT);



  const struct argp_child argp_children[] =
    {
      {
  .argp = dwfl_standard_argp (),
  .header = "Input selection options:",
  .group = 1,
      },
      { .argp = NULL },
    };
  const struct argp argp =
    {
      .options = options,
      .parser = parse_opt,
      .children = argp_children,
      .args_doc = "STRIPPED-FILE DEBUG-FILE\n[MODULE...]",
      .doc = "char"
    };

  int argc = 4;
  char * argv[] = {"abcd", "-n", "-e", filename};
  int remaining;
  struct arg_info info = { .args = NULL };
  error_t result = argp_parse (&argp, argc, argv, 0, &remaining, &info);
  if (result == ENOSYS)
    assert (info.dwfl == NULL);
  else if (result)
    return EXIT_FAILURE;
  assert (info.args != NULL);

  data = handle_implicit_modules (&info);

  dwfl_end (info.dwfl);


  return data;
}

static PyMethodDef ElfDataMethods[] = {
    {"get_buildid",  elfdata_parseelf, METH_VARARGS,
     "Does some magic"},
    /*{"pythoncall",  kabireport_pythoncall, METH_VARARGS,
     "pass the python functions here"},*/
    {NULL, NULL, 0, NULL}        /* Sentinel */
};


PyMODINIT_FUNC
initelfdata(void)
{
        (void) Py_InitModule("elfdata", ElfDataMethods);
}


