/* ldscript_json_impl.inc
 * Standalone implementation block for --dump-script-json.
 * Include from src/binutils/ld/ldlang.c only.
 */

#include "ldjson_compat.h"

/* ==== Core serializer implementation ==== */

static void
json_indent (FILE *f, unsigned int depth)
{
  while (depth-- > 0)
    fputs ("  ", f);
}

static void
json_write_escaped_string (FILE *f, const char *s)
{
  const unsigned char *p;

  fputc ('"', f);
  if (s != NULL)
    for (p = (const unsigned char *) s; *p != '\0'; p++)
      switch (*p)
	{
	case '"':
	  fputs ("\\\"", f);
	  break;
	case '\\':
	  fputs ("\\\\", f);
	  break;
	case '\b':
	  fputs ("\\b", f);
	  break;
	case '\f':
	  fputs ("\\f", f);
	  break;
	case '\n':
	  fputs ("\\n", f);
	  break;
	case '\r':
	  fputs ("\\r", f);
	  break;
	case '\t':
	  fputs ("\\t", f);
	  break;
	default:
	  if (*p < 0x20)
	    fprintf (f, "\\u%04x", (unsigned int) *p);
	  else if (*p >= 0x80)
	    fprintf (f, "\\u00%02x", (unsigned int) *p);
	  else
	    fputc (*p, f);
	  break;
	}
  fputc ('"', f);
}

static void
json_write_string_or_null (FILE *f, const char *s)
{
  if (s == NULL)
    fputs ("null", f);
  else
    json_write_escaped_string (f, s);
}

static void
json_write_hex_vma (FILE *f, bfd_vma value)
{
  static const char hex[] = "0123456789abcdef";
  bool started = false;
  unsigned int i;

  fputs ("\"0x", f);
  for (i = (unsigned int) (sizeof (bfd_vma) * 2); i > 0; i--)
    {
      unsigned int shift = (i - 1) * 4;
      unsigned int nibble = (unsigned int) ((value >> shift) & 0xf);

      if (nibble != 0 || started || i == 1)
	{
	  fputc (hex[nibble], f);
	  started = true;
	}
    }
  fputc ('"', f);
}

static void
json_write_id (FILE *f, char prefix, unsigned int index)
{
  fprintf (f, "\"%c%06u\"", prefix, index);
}

static void
json_write_prefixed_id (FILE *f, const char *prefix, unsigned int index)
{
  fprintf (f, "\"%s%06u\"", prefix, index);
}

static bool
json_strings_equal (const char *a, const char *b)
{
  if (a == b)
    return true;
  if (a == NULL || b == NULL)
    return false;
  return strcmp (a, b) == 0;
}

static void
json_flag_letters (flagword flag, char *buf, size_t bufsize)
{
  size_t i = 0;

  if (bufsize == 0)
    return;

#define JSON_ADD_FLAG(CH)						\
  do								\
    {								\
      if (i + 1 < bufsize)					\
	buf[i++] = (CH);					\
    }								\
  while (0)

  if (flag & SEC_ALLOC)
    JSON_ADD_FLAG ('a');
  if (flag & SEC_CODE)
    JSON_ADD_FLAG ('x');
  if (flag & SEC_READONLY)
    JSON_ADD_FLAG ('r');
  if (flag & SEC_DATA)
    JSON_ADD_FLAG ('w');
  if (flag & SEC_LOAD)
    JSON_ADD_FLAG ('l');

#undef JSON_ADD_FLAG

  buf[i] = '\0';
}

static void
json_section_flag_letters (flagword flag, char *buf, size_t bufsize)
{
  size_t i = 0;

  if (bufsize == 0)
    return;

#define JSON_ADD_SECTION_FLAG(CH)					\
  do								\
    {								\
      if (i + 1 < bufsize)					\
	buf[i++] = (CH);					\
    }								\
  while (0)

  if (flag & SEC_ALLOC)
    JSON_ADD_SECTION_FLAG ('a');
  if (flag & SEC_CODE)
    JSON_ADD_SECTION_FLAG ('x');
  else if (flag & SEC_READONLY)
    JSON_ADD_SECTION_FLAG ('r');
  else if (flag & SEC_ALLOC)
    JSON_ADD_SECTION_FLAG ('w');

#undef JSON_ADD_SECTION_FLAG

  buf[i] = '\0';
}

static void
json_write_flags_object (FILE *f, flagword flags, unsigned int indent)
{
  char letters[16];

  json_section_flag_letters (flags, letters, sizeof (letters));

  fputs ("{\n", f);
  json_indent (f, indent + 1);
  fputs ("\"letters\": ", f);
  json_write_escaped_string (f, letters);
  fputs (",\n", f);
  json_indent (f, indent + 1);
  fputs ("\"hex\": ", f);
  json_write_hex_vma (f, (bfd_vma) flags);
  fputc ('\n', f);
  json_indent (f, indent);
  fputc ('}', f);
}

static bool
json_is_output_section_materialized (asection *sec)
{
  return (sec != NULL
          && sec->owner == link_info.output_bfd
          && (sec->flags & SEC_EXCLUDE) == 0);
}

static bool
json_is_input_section_materialized (asection *sec)
{
  return (sec != NULL
          && (sec->flags & SEC_EXCLUDE) == 0
          && sec->output_section != NULL
          && sec->output_section->owner == link_info.output_bfd
          && (sec->output_section->flags & SEC_EXCLUDE) == 0);
}

static asection *
json_get_final_output_section (asection *sec)
{
  if (json_is_output_section_materialized (sec))
    return sec;
  if (json_is_input_section_materialized (sec))
    return sec->output_section;
  return NULL;
}

static bool
json_value_within_range (bfd_vma start, bfd_size_type size, bfd_vma value)
{
  bfd_vma span = TO_ADDR (size);

  if (value < start)
    return false;
  if (span == 0)
    return value == start;
  return (value - start) <= span;
}

static bool
json_output_section_contains_value (asection *sec, bfd_vma value)
{
  if (sec == NULL)
    return false;

  if (json_value_within_range (sec->vma, sec->size, value))
    return true;
  if (sec->lma != sec->vma
      && json_value_within_range (sec->lma, sec->size, value))
    return true;

  return false;
}

static asection *
json_find_output_section_for_value (bfd_vma value)
{
  lang_output_section_statement_type *os;

  for (os = (void *) lang_os_list.head; os != NULL; os = os->next)
    {
      asection *outsec = os->bfd_section;

      if (outsec != NULL && json_output_section_contains_value (outsec, value))
        return outsec;
    }

  return NULL;
}

static const char *
json_statement_type_name (enum statement_enum type)
{
  switch (type)
    {
    case lang_address_statement_enum:
      return "address";
    case lang_assignment_statement_enum:
      return "assignment";
    case lang_data_statement_enum:
      return "data";
    case lang_fill_statement_enum:
      return "fill";
    case lang_group_statement_enum:
      return "group";
    case lang_input_section_enum:
      return "input_section";
    case lang_input_matcher_enum:
      return "input_matcher";
    case lang_input_statement_enum:
      return "input_statement";
    case lang_insert_statement_enum:
      return "insert";
    case lang_output_section_statement_enum:
      return "output_section";
    case lang_output_statement_enum:
      return "output";
    case lang_padding_statement_enum:
      return "padding";
    case lang_reloc_statement_enum:
      return "reloc";
    case lang_target_statement_enum:
      return "target";
    case lang_wild_statement_enum:
      return "wild";
    case lang_constructors_statement_enum:
      return "constructors";
    case lang_object_symbols_statement_enum:
      return "object_symbols";
    default:
      return "unknown";
    }
}

static const char *
json_node_class_name (enum node_tree_enum node_class)
{
  switch (node_class)
    {
    case etree_binary:
      return "etree_binary";
    case etree_trinary:
      return "etree_trinary";
    case etree_unary:
      return "etree_unary";
    case etree_name:
      return "etree_name";
    case etree_assign:
      return "etree_assign";
    case etree_provide:
      return "etree_provide";
    case etree_provided:
      return "etree_provided";
    case etree_value:
      return "etree_value";
    case etree_assert:
      return "etree_assert";
    case etree_rel:
      return "etree_rel";
    default:
      return "unknown";
    }
}

static const char *
json_sort_type_name (sort_type type)
{
  switch (type)
    {
    case none:
      return "none";
    case by_name:
      return "by_name";
    case by_alignment:
      return "by_alignment";
    case by_name_alignment:
      return "by_name_alignment";
    case by_alignment_name:
      return "by_alignment_name";
    case by_none:
      return "by_none";
    case by_init_priority:
      return "by_init_priority";
    default:
      return "unknown";
    }
}

static const char *
json_data_type_name (unsigned int type)
{
  switch (type)
    {
    case BYTE:
      return "BYTE";
    case SHORT:
      return "SHORT";
    case LONG:
      return "LONG";
    case QUAD:
      return "QUAD";
    case SQUAD:
      return "SQUAD";
    default:
      return "UNKNOWN";
    }
}

static void
json_dump_name_list (FILE *f, name_list *list, unsigned int indent)
{
  bool first = true;

  fputc ('[', f);
  if (list != NULL)
    {
      fputc ('\n', f);
      for (; list != NULL; list = list->next)
	{
	  if (!first)
	    fputs (",\n", f);
	  first = false;
	  json_indent (f, indent + 1);
	  json_write_escaped_string (f, list->name);
	}
      fputc ('\n', f);
      json_indent (f, indent);
    }
  fputc (']', f);
}

static void
json_dump_flag_info (FILE *f, struct flag_info *info, unsigned int indent)
{
  bool first = true;
  struct flag_info_list *list;

  if (info == NULL)
    {
      fputs ("null", f);
      return;
    }

  fputs ("{\n", f);
  json_indent (f, indent + 1);
  fputs ("\"only_with_flags_hex\": ", f);
  fprintf (f, "\"0x%lx\",\n", (unsigned long) info->only_with_flags);
  json_indent (f, indent + 1);
  fputs ("\"not_with_flags_hex\": ", f);
  fprintf (f, "\"0x%lx\",\n", (unsigned long) info->not_with_flags);
  json_indent (f, indent + 1);
  fputs ("\"flags_initialized\": ", f);
  fputs (info->flags_initialized ? "true,\n" : "false,\n", f);
  json_indent (f, indent + 1);
  fputs ("\"flag_names\": [", f);

  for (list = info->flag_list; list != NULL; list = list->next)
    {
      if (first)
	{
	  fputc ('\n', f);
	  first = false;
	}
      else
	fputs (",\n", f);

      json_indent (f, indent + 2);
      fputs ("{ \"name\": ", f);
      json_write_string_or_null (f, list->name);
      fputs (", \"with\": ", f);
      json_write_escaped_string (f,
				 list->with == with_flags
				 ? "with_flags" : "without_flags");
      fputs (", \"valid\": ", f);
      fputs (list->valid ? "true" : "false", f);
      fputs (" }", f);
    }

  if (!first)
    {
      fputc ('\n', f);
      json_indent (f, indent + 1);
    }
  fputs ("]\n", f);
  json_indent (f, indent);
  fputc ('}', f);
}

static void
json_dump_fill_bytes (FILE *f, fill_type *fill)
{
  size_t i;

  if (fill == NULL)
    {
      fputs ("null", f);
      return;
    }

  fputc ('"', f);
  for (i = 0; i < fill->size; i++)
    fprintf (f, "%02x", fill->data[i]);
  fputc ('"', f);
}

static void
json_dump_input_flags (FILE *f,
		       const struct lang_input_statement_flags *flags,
		       unsigned int indent)
{
  fputs ("{\n", f);
  json_indent (f, indent + 1);
  fputs ("\"maybe_archive\": ", f);
  fputs (flags->maybe_archive ? "true,\n" : "false,\n", f);
  json_indent (f, indent + 1);
  fputs ("\"full_name_provided\": ", f);
  fputs (flags->full_name_provided ? "true,\n" : "false,\n", f);
  json_indent (f, indent + 1);
  fputs ("\"search_dirs\": ", f);
  fputs (flags->search_dirs ? "true,\n" : "false,\n", f);
  json_indent (f, indent + 1);
  fputs ("\"sysrooted\": ", f);
  fputs (flags->sysrooted ? "true,\n" : "false,\n", f);
  json_indent (f, indent + 1);
  fputs ("\"just_syms\": ", f);
  fputs (flags->just_syms ? "true,\n" : "false,\n", f);
  json_indent (f, indent + 1);
  fputs ("\"dynamic\": ", f);
  fputs (flags->dynamic ? "true,\n" : "false,\n", f);
  json_indent (f, indent + 1);
  fputs ("\"add_DT_NEEDED_for_dynamic\": ", f);
  fputs (flags->add_DT_NEEDED_for_dynamic ? "true,\n" : "false,\n", f);
  json_indent (f, indent + 1);
  fputs ("\"add_DT_NEEDED_for_regular\": ", f);
  fputs (flags->add_DT_NEEDED_for_regular ? "true,\n" : "false,\n", f);
  json_indent (f, indent + 1);
  fputs ("\"whole_archive\": ", f);
  fputs (flags->whole_archive ? "true,\n" : "false,\n", f);
  json_indent (f, indent + 1);
  fputs ("\"loaded\": ", f);
  fputs (flags->loaded ? "true,\n" : "false,\n", f);
  json_indent (f, indent + 1);
  fputs ("\"real\": ", f);
  fputs (flags->real ? "true,\n" : "false,\n", f);
  json_indent (f, indent + 1);
  fputs ("\"missing_file\": ", f);
  fputs (flags->missing_file ? "true,\n" : "false,\n", f);
  json_indent (f, indent + 1);
  fputs ("\"reload\": ", f);
#if BFD_SUPPORTS_PLUGINS
  fputs (flags->reload ? "true,\n" : "false,\n", f);
  json_indent (f, indent + 1);
  fputs ("\"claimed\": ", f);
  fputs (flags->claimed ? "true,\n" : "false,\n", f);
  json_indent (f, indent + 1);
  fputs ("\"claim_archive\": ", f);
  fputs (flags->claim_archive ? "true,\n" : "false,\n", f);
  json_indent (f, indent + 1);
  fputs ("\"lto_output\": ", f);
  fputs (flags->lto_output ? "true\n" : "false\n", f);
#else
  fputs (flags->reload ? "true\n" : "false\n", f);
#endif
  json_indent (f, indent);
  fputc ('}', f);
}

static void
json_dump_input_section_ref (FILE *f,
			     asection *section,
			     lang_input_statement_type *input_stmt,
			     unsigned int indent)
{
  char flags_letters[8];
  bfd_vma addr = 0;
  bool has_addr = false;

  fputs ("{\n", f);

  if (section != NULL)
    {
      init_opb (section);
      if (section->output_section != NULL
	  && section->output_section->owner == link_info.output_bfd)
	{
	  addr = section->output_section->vma + section->output_offset;
	  has_addr = true;
	}
      json_flag_letters (section->flags, flags_letters, sizeof (flags_letters));
    }
  else
    flags_letters[0] = '\0';

  json_indent (f, indent + 1);
  fputs ("\"name\": ", f);
  json_write_string_or_null (f, section != NULL ? section->name : NULL);
  fputs (",\n", f);

  json_indent (f, indent + 1);
  fputs ("\"owner_file\": ", f);
  json_write_string_or_null (f,
			     section != NULL && section->owner != NULL
			     ? bfd_get_filename (section->owner) : NULL);
  fputs (",\n", f);

  json_indent (f, indent + 1);
  fputs ("\"input_statement_file\": ", f);
  json_write_string_or_null (f,
			     input_stmt != NULL ? input_stmt->filename : NULL);
  fputs (",\n", f);

  json_indent (f, indent + 1);
  fputs ("\"address_hex\": ", f);
  if (has_addr)
    json_write_hex_vma (f, addr);
  else
    fputs ("null", f);
  fputs (",\n", f);

  json_indent (f, indent + 1);
  fputs ("\"size_hex\": ", f);
  if (section != NULL)
    json_write_hex_vma (f, TO_ADDR (section->size));
  else
    fputs ("null", f);
  fputs (",\n", f);

  json_indent (f, indent + 1);
  fputs ("\"rawsize_hex\": ", f);
  if (section != NULL)
    json_write_hex_vma (f, TO_ADDR (section->rawsize));
  else
    fputs ("null", f);
  fputs (",\n", f);

  json_indent (f, indent + 1);
  fputs ("\"output_offset_hex\": ", f);
  if (section != NULL)
    json_write_hex_vma (f, section->output_offset);
  else
    fputs ("null", f);
  fputs (",\n", f);

  json_indent (f, indent + 1);
  fputs ("\"output_section_name\": ", f);
  json_write_string_or_null
    (f, section != NULL && section->output_section != NULL
     ? section->output_section->name : NULL);
  fputs (",\n", f);

  json_indent (f, indent + 1);
  fputs ("\"flags_hex\": ", f);
  if (section != NULL)
    fprintf (f, "\"0x%lx\"", (unsigned long) section->flags);
  else
    fputs ("null", f);
  fputs (",\n", f);

  json_indent (f, indent + 1);
  fputs ("\"flags_letters\": ", f);
  if (section != NULL)
    json_write_escaped_string (f, flags_letters);
  else
    fputs ("null", f);
  fputs ("\n", f);

  json_indent (f, indent);
  fputc ('}', f);
}

static void json_dump_statement_list (FILE *f,
				      lang_statement_union_type *s,
				      unsigned int indent);

static void
json_dump_wildcard_spec (FILE *f,
			 const struct wildcard_spec *spec,
			 unsigned int indent)
{
  fputs ("{\n", f);
  json_indent (f, indent + 1);
  fputs ("\"name\": ", f);
  json_write_string_or_null (f, spec->name);
  fputs (",\n", f);
  json_indent (f, indent + 1);
  fputs ("\"sorted\": ", f);
  json_write_escaped_string (f, json_sort_type_name (spec->sorted));
  fputs (",\n", f);
  json_indent (f, indent + 1);
  fputs ("\"reversed\": ", f);
  fputs (spec->reversed ? "true,\n" : "false,\n", f);
  json_indent (f, indent + 1);
  fputs ("\"namelen\": ", f);
  fprintf (f, "%lu,\n", (unsigned long) spec->namelen);
  json_indent (f, indent + 1);
  fputs ("\"prefixlen\": ", f);
  fprintf (f, "%lu,\n", (unsigned long) spec->prefixlen);
  json_indent (f, indent + 1);
  fputs ("\"suffixlen\": ", f);
  fprintf (f, "%lu,\n", (unsigned long) spec->suffixlen);
  json_indent (f, indent + 1);
  fputs ("\"exclude_files\": ", f);
  json_dump_name_list (f, spec->exclude_name_list, indent + 1);
  fputs (",\n", f);
  json_indent (f, indent + 1);
  fputs ("\"section_flag_info\": ", f);
  json_dump_flag_info (f, spec->section_flag_list, indent + 1);
  fputs ("\n", f);
  json_indent (f, indent);
  fputc ('}', f);
}

static void
json_dump_statement (FILE *f, lang_statement_union_type *s, unsigned int indent)
{
  bool first = true;

#define JSON_FIELD_PREFIX()						\
  do								\
    {								\
      if (!first)						\
	fputs (",\n", f);						\
      first = false;						\
      json_indent (f, indent + 1);				\
    }								\
  while (0)

  fputs ("{\n", f);
  JSON_FIELD_PREFIX ();
  fputs ("\"type\": ", f);
  json_write_escaped_string (f, json_statement_type_name (s->header.type));
  fputs (",\n", f);
  json_indent (f, indent + 1);
  fputs ("\"type_id\": ", f);
  fprintf (f, "%d", s->header.type);

  switch (s->header.type)
    {
    case lang_address_statement_enum:
      JSON_FIELD_PREFIX ();
      fputs ("\"section_name\": ", f);
      json_write_string_or_null (f, s->address_statement.section_name);
      break;

    case lang_assignment_statement_enum:
      {
	etree_type *exp = s->assignment_statement.exp;
	JSON_FIELD_PREFIX ();
	fputs ("\"node_class\": ", f);
	json_write_escaped_string (f, json_node_class_name (exp->type.node_class));
	JSON_FIELD_PREFIX ();
	fputs ("\"node_class_id\": ", f);
	fprintf (f, "%d", exp->type.node_class);
	if (exp->type.node_class == etree_assign
	    || exp->type.node_class == etree_provide
	    || exp->type.node_class == etree_provided)
	  {
	    JSON_FIELD_PREFIX ();
	    fputs ("\"symbol\": ", f);
	    json_write_string_or_null (f, exp->assign.dst);
	    JSON_FIELD_PREFIX ();
	    fputs ("\"hidden\": ", f);
	    fputs (exp->assign.hidden ? "true" : "false", f);
	  }
	else if (exp->type.node_class == etree_assert)
	  {
	    JSON_FIELD_PREFIX ();
	    fputs ("\"message\": ", f);
	    json_write_string_or_null (f, exp->assert_s.message);
	  }
      }
      break;

    case lang_data_statement_enum:
      JSON_FIELD_PREFIX ();
      fputs ("\"data_type\": ", f);
      json_write_escaped_string (f,
				 json_data_type_name (s->data_statement.type));
      JSON_FIELD_PREFIX ();
      fputs ("\"value_hex\": ", f);
      json_write_hex_vma (f, s->data_statement.value);
      JSON_FIELD_PREFIX ();
      fputs ("\"output_offset_hex\": ", f);
      json_write_hex_vma (f, s->data_statement.output_offset);
      break;

    case lang_fill_statement_enum:
      JSON_FIELD_PREFIX ();
      fputs ("\"fill_hex\": ", f);
      json_dump_fill_bytes (f, s->fill_statement.fill);
      break;

    case lang_group_statement_enum:
      JSON_FIELD_PREFIX ();
      fputs ("\"children\": ", f);
      json_dump_statement_list (f, s->group_statement.children.head, indent + 1);
      break;

    case lang_input_section_enum:
      JSON_FIELD_PREFIX ();
      fputs ("\"section\": ", f);
      json_dump_input_section_ref (f, s->input_section.section, NULL, indent + 1);
      break;

    case lang_input_matcher_enum:
      JSON_FIELD_PREFIX ();
      fputs ("\"pattern_name\": ", f);
      if (s->input_matcher.pattern != NULL)
	{
	  const struct wildcard_list *pat;
	  pat = (const struct wildcard_list *) s->input_matcher.pattern;
	  json_write_string_or_null (f, pat->spec.name);
	}
      else
	fputs ("null", f);
      JSON_FIELD_PREFIX ();
      fputs ("\"section\": ", f);
      json_dump_input_section_ref (f, s->input_matcher.section,
				   s->input_matcher.input_stmt, indent + 1);
      break;

    case lang_input_statement_enum:
      JSON_FIELD_PREFIX ();
      fputs ("\"filename\": ", f);
      json_write_string_or_null (f, s->input_statement.filename);
      JSON_FIELD_PREFIX ();
      fputs ("\"local_sym_name\": ", f);
      json_write_string_or_null (f, s->input_statement.local_sym_name);
      JSON_FIELD_PREFIX ();
      fputs ("\"target\": ", f);
      json_write_string_or_null (f, s->input_statement.target);
      JSON_FIELD_PREFIX ();
      fputs ("\"flags\": ", f);
      json_dump_input_flags (f, &s->input_statement.flags, indent + 1);
      break;

    case lang_insert_statement_enum:
      JSON_FIELD_PREFIX ();
      fputs ("\"where\": ", f);
      json_write_string_or_null (f, s->insert_statement.where);
      JSON_FIELD_PREFIX ();
      fputs ("\"is_before\": ", f);
      fputs (s->insert_statement.is_before ? "true" : "false", f);
      break;

    case lang_output_section_statement_enum:
      JSON_FIELD_PREFIX ();
      fputs ("\"name\": ", f);
      json_write_string_or_null (f, s->output_section_statement.name);
      JSON_FIELD_PREFIX ();
      fputs ("\"children\": ", f);
      json_dump_statement_list (f, s->output_section_statement.children.head,
				indent + 1);
      break;

    case lang_output_statement_enum:
      JSON_FIELD_PREFIX ();
      fputs ("\"name\": ", f);
      json_write_string_or_null (f, s->output_statement.name);
      break;

    case lang_padding_statement_enum:
      JSON_FIELD_PREFIX ();
      fputs ("\"size_hex\": ", f);
      if (s->padding_statement.output_section != NULL)
	{
	  init_opb (s->padding_statement.output_section);
	  json_write_hex_vma (f, TO_ADDR (s->padding_statement.size));
	}
      else
	json_write_hex_vma (f, s->padding_statement.size);
      JSON_FIELD_PREFIX ();
      fputs ("\"output_offset_hex\": ", f);
      json_write_hex_vma (f, s->padding_statement.output_offset);
      JSON_FIELD_PREFIX ();
      fputs ("\"fill_hex\": ", f);
      json_dump_fill_bytes (f, s->padding_statement.fill);
      break;

    case lang_reloc_statement_enum:
      JSON_FIELD_PREFIX ();
      fputs ("\"reloc_name\": ", f);
      json_write_string_or_null (f,
				 s->reloc_statement.howto != NULL
				 ? s->reloc_statement.howto->name : NULL);
      JSON_FIELD_PREFIX ();
      fputs ("\"symbol_name\": ", f);
      json_write_string_or_null (f, s->reloc_statement.name);
      JSON_FIELD_PREFIX ();
      fputs ("\"section_name\": ", f);
      json_write_string_or_null (f,
				 s->reloc_statement.section != NULL
				 ? s->reloc_statement.section->name : NULL);
      JSON_FIELD_PREFIX ();
      fputs ("\"addend_hex\": ", f);
      json_write_hex_vma (f, s->reloc_statement.addend_value);
      break;

    case lang_target_statement_enum:
      JSON_FIELD_PREFIX ();
      fputs ("\"target\": ", f);
      json_write_string_or_null (f, s->target_statement.target);
      break;

    case lang_wild_statement_enum:
      {
	struct wildcard_list *sec;
	lang_statement_union_type *m;
	bool first_spec = true;
	bool first_match = true;

	JSON_FIELD_PREFIX ();
	fputs ("\"filename\": ", f);
	json_write_string_or_null (f, s->wild_statement.filename);
	JSON_FIELD_PREFIX ();
	fputs ("\"keep_sections\": ", f);
	fputs (s->wild_statement.keep_sections ? "true" : "false", f);
	JSON_FIELD_PREFIX ();
	fputs ("\"filenames_sorted\": ", f);
	fputs (s->wild_statement.filenames_sorted ? "true" : "false", f);
	JSON_FIELD_PREFIX ();
	fputs ("\"filenames_reversed\": ", f);
	fputs (s->wild_statement.filenames_reversed ? "true" : "false", f);
	JSON_FIELD_PREFIX ();
	fputs ("\"any_specs_sorted\": ", f);
	fputs (s->wild_statement.any_specs_sorted ? "true" : "false", f);
	JSON_FIELD_PREFIX ();
	fputs ("\"exclude_files\": ", f);
	json_dump_name_list (f, s->wild_statement.exclude_name_list, indent + 1);
	JSON_FIELD_PREFIX ();
	fputs ("\"section_flag_info\": ", f);
	json_dump_flag_info (f, s->wild_statement.section_flag_list, indent + 1);

	JSON_FIELD_PREFIX ();
	fputs ("\"section_patterns\": [", f);
	for (sec = s->wild_statement.section_list; sec != NULL; sec = sec->next)
	  {
	    if (first_spec)
	      {
		fputc ('\n', f);
		first_spec = false;
	      }
	    else
	      fputs (",\n", f);
	    json_indent (f, indent + 2);
	    json_dump_wildcard_spec (f, &sec->spec, indent + 2);
	  }
	if (!first_spec)
	  {
	    fputc ('\n', f);
	    json_indent (f, indent + 1);
	  }
	fputc (']', f);

	JSON_FIELD_PREFIX ();
	fputs ("\"matching_sections\": [", f);
	for (m = s->wild_statement.matching_sections.head;
	     m != NULL;
	     m = m->header.next)
	  {
	    const struct wildcard_list *pat = NULL;

	    if (m->header.type != lang_input_matcher_enum)
	      continue;

	    if (first_match)
	      {
		fputc ('\n', f);
		first_match = false;
	      }
	    else
	      fputs (",\n", f);

	    if (m->input_matcher.pattern != NULL)
	      pat = (const struct wildcard_list *) m->input_matcher.pattern;

	    json_indent (f, indent + 2);
	    fputs ("{\n", f);
	    json_indent (f, indent + 3);
	    fputs ("\"pattern_name\": ", f);
	    json_write_string_or_null (f, pat != NULL ? pat->spec.name : NULL);
	    fputs (",\n", f);
	    json_indent (f, indent + 3);
	    fputs ("\"section\": ", f);
	    json_dump_input_section_ref (f, m->input_matcher.section,
					 m->input_matcher.input_stmt,
					 indent + 3);
	    fputc ('\n', f);
	    json_indent (f, indent + 2);
	    fputc ('}', f);
	  }
	if (!first_match)
	  {
	    fputc ('\n', f);
	    json_indent (f, indent + 1);
	  }
	fputc (']', f);

	JSON_FIELD_PREFIX ();
	fputs ("\"children\": ", f);
	json_dump_statement_list (f, s->wild_statement.children.head, indent + 1);
      }
      break;

    case lang_constructors_statement_enum:
    case lang_object_symbols_statement_enum:
    default:
      break;
    }

  fputc ('\n', f);
  json_indent (f, indent);
  fputc ('}', f);

#undef JSON_FIELD_PREFIX
}

static void
json_dump_statement_list (FILE *f,
			  lang_statement_union_type *s,
			  unsigned int indent)
{
  bool first = true;

  fputc ('[', f);
  for (; s != NULL; s = s->header.next)
    {
      if (first)
	{
	  fputc ('\n', f);
	  first = false;
	}
      else
	fputs (",\n", f);

      json_indent (f, indent + 1);
      json_dump_statement (f, s, indent + 1);
    }
  if (!first)
    {
      fputc ('\n', f);
      json_indent (f, indent);
    }
  fputc (']', f);
}

static void
json_dump_core_regions (FILE *f, unsigned int indent);

static void
json_dump_core_sections (FILE *f, unsigned int indent);

static void
json_dump_memory_regions (FILE *f, unsigned int indent)
{
  json_dump_core_regions (f, indent);
}

static void
json_emit_subsection_expr (FILE *f,
			   const char *file_pattern,
			   const char *section_pattern,
			   unsigned int indent,
			   bool *first)
{
  const char *fp = file_pattern != NULL ? file_pattern : "*";
  const char *sp = section_pattern != NULL ? section_pattern : "*";
  size_t fp_len = strlen (fp);
  size_t sp_len = strlen (sp);
  char *expr = (char *) xmalloc (fp_len + sp_len + 3);

  memcpy (expr, fp, fp_len);
  expr[fp_len] = '(';
  memcpy (expr + fp_len + 1, sp, sp_len);
  expr[fp_len + 1 + sp_len] = ')';
  expr[fp_len + 2 + sp_len] = '\0';

  if (*first)
    {
      fputc ('\n', f);
      *first = false;
    }
  else
    fputs (",\n", f);

  json_indent (f, indent + 1);
  json_write_escaped_string (f, expr);
  free (expr);
}

static void
json_collect_output_section_subsections (FILE *f,
					 lang_statement_union_type *s,
					 unsigned int indent,
					 bool *first)
{
  for (; s != NULL; s = s->header.next)
    switch (s->header.type)
      {
      case lang_wild_statement_enum:
	{
	  struct wildcard_list *sec;
	  const char *file_pattern =
	    s->wild_statement.filename != NULL ? s->wild_statement.filename : "*";

	  if (s->wild_statement.section_list == NULL)
	    json_emit_subsection_expr (f, file_pattern, "*", indent, first);
	  else
	    for (sec = s->wild_statement.section_list;
		 sec != NULL;
		 sec = sec->next)
	      json_emit_subsection_expr (f, file_pattern, sec->spec.name,
					 indent, first);

	  json_collect_output_section_subsections (f,
						   s->wild_statement.children.head,
						   indent, first);
	}
	break;

      case lang_group_statement_enum:
	json_collect_output_section_subsections (f,
						 s->group_statement.children.head,
						 indent, first);
	break;

      case lang_output_section_statement_enum:
	json_collect_output_section_subsections (f,
						 s->output_section_statement.children.head,
						 indent, first);
	break;

      default:
	break;
      }
}

static void
json_dump_output_section_subsections (FILE *f,
				      lang_output_section_statement_type *os,
				      unsigned int indent)
{
  bool first = true;

  fputc ('[', f);
  json_collect_output_section_subsections (f, os->children.head, indent, &first);
  if (!first)
    {
      fputc ('\n', f);
      json_indent (f, indent);
    }
  fputc (']', f);
}

static void
json_dump_output_sections (FILE *f, unsigned int indent)
{
  json_dump_core_sections (f, indent);
}

static void
json_dump_output_info (FILE *f, unsigned int indent)
{
  fputs ("{\n", f);
  json_indent (f, indent + 1);
  fputs ("\"entry_symbol\": ", f);
  json_write_string_or_null (f, entry_symbol.name);
  fputc ('\n', f);
  json_indent (f, indent);
  fputc ('}', f);
}

struct json_input_section_entry
{
  asection *section;
  bool discarded;
  const char *discard_reason;
};

struct json_input_section_vec
{
  struct json_input_section_entry *items;
  size_t count;
  size_t cap;
};

static void
json_input_section_vec_push (struct json_input_section_vec *vec,
                             asection *section,
                             bool discarded,
                             const char *discard_reason)
{
  if (vec->count == vec->cap)
    {
      size_t new_cap = vec->cap == 0 ? 128 : vec->cap * 2;
      vec->items = (struct json_input_section_entry *)
        xrealloc (vec->items, new_cap * sizeof (*vec->items));
      vec->cap = new_cap;
    }

  vec->items[vec->count].section = section;
  vec->items[vec->count].discarded = discarded;
  vec->items[vec->count].discard_reason = discard_reason;
  vec->count++;
}

static const char *
json_input_section_discard_reason (asection *section)
{
  if (section == NULL)
    return "discarded";

  if ((section->flags & SEC_EXCLUDE) != 0)
    {
      if ((link_info.strip == strip_debugger || link_info.strip == strip_all)
          && (section->flags & SEC_DEBUGGING) != 0)
        return "strip_debug";
      if (config.no_section_header && (section->flags & SEC_ALLOC) == 0)
        return "no_section_header";
      return "gc_sections";
    }

  if (section->output_section != NULL && bfd_is_abs_section (section->output_section))
    return "discard_script";

  return "discarded";
}

static bool
json_input_section_is_discarded (asection *section)
{
  if (section == NULL)
    return false;

  return (!json_is_input_section_materialized (section)
          && (section->flags & (SEC_LINKER_CREATED | SEC_KEEP)) == 0);
}

static void
json_collect_input_sections (struct json_input_section_vec *vec)
{
  vec->items = NULL;
  vec->count = 0;
  vec->cap = 0;

  LANG_FOR_EACH_INPUT_STATEMENT (file)
    {
      asection *sec;

      if (file == NULL
          || file->the_bfd == NULL
          || (file->the_bfd->flags & (BFD_LINKER_CREATED | DYNAMIC)) != 0
          || file->flags.just_syms)
        continue;

      for (sec = file->the_bfd->sections; sec != NULL; sec = sec->next)
        {
          bool discarded;

          if ((sec->flags & SEC_LINKER_CREATED) != 0)
            continue;

          discarded = json_input_section_is_discarded (sec);
          json_input_section_vec_push (vec, sec, discarded,
                                       discarded
                                       ? json_input_section_discard_reason (sec)
                                       : NULL);
        }
    }
}

static void
json_free_input_sections (struct json_input_section_vec *vec)
{
  free (vec->items);
  vec->items = NULL;
  vec->count = 0;
  vec->cap = 0;
}

static bool
json_find_output_section_id (asection *sec, char *buf, size_t bufsize)
{
  lang_output_section_statement_type *os;
  unsigned int idx = 0;

  for (os = (void *) lang_os_list.head; os != NULL; os = os->next)
    {
      idx++;
      if (os->bfd_section == sec)
        {
          if (buf != NULL && bufsize != 0)
            sprintf (buf, "s%06u", idx);
          return true;
        }
    }

  return false;
}

static asection *
json_find_unique_output_section_by_name (const char *name)
{
  lang_output_section_statement_type *os;
  asection *match = NULL;

  if (name == NULL)
    return NULL;

  for (os = (void *) lang_os_list.head; os != NULL; os = os->next)
    {
      if (os->bfd_section == NULL || os->name == NULL)
        continue;
      if (strcmp (os->name, name) != 0)
        continue;

      if (match != NULL)
        return NULL;

      match = os->bfd_section;
    }

  return match;
}

static bool
json_find_input_section_id (const struct json_input_section_vec *vec,
                            asection *sec,
                            char *buf,
                            size_t bufsize)
{
  size_t i;

  if (vec == NULL || sec == NULL)
    return false;

  for (i = 0; i < vec->count; i++)
    if (vec->items[i].section == sec)
      {
        if (buf != NULL && bufsize != 0)
          sprintf (buf, "is%06u", (unsigned int) (i + 1));
        return true;
      }

  return false;
}

static bool
json_input_section_name_seen_before (const struct json_input_section_vec *vec,
                                     size_t stop_index,
                                     const char *name)
{
  size_t i;

  for (i = 0; i < stop_index; i++)
    if (json_strings_equal (vec->items[i].section != NULL
                            ? vec->items[i].section->name : NULL,
                            name))
      return true;
  return false;
}

static void
json_dump_input_sections (FILE *f,
                          unsigned int indent,
                          const struct json_input_section_vec *vec)
{
  size_t i;
  bool first_item = true;
  bool first_name = true;
  bool first_null = true;

  fputs ("{\n", f);

  json_indent (f, indent + 1);
  fputs ("\"items\": [", f);
  for (i = 0; i < vec->count; i++)
    {
      asection *sec = vec->items[i].section;
      asection *outsec = json_get_final_output_section (sec);
      asection *outsec_for_id = outsec;

      if (outsec_for_id == NULL
          && sec != NULL
          && !vec->items[i].discarded)
        outsec_for_id = json_find_unique_output_section_by_name (sec->name);

      if (first_item)
        {
          fputc ('\n', f);
          first_item = false;
        }
      else
        fputs (",\n", f);

      json_indent (f, indent + 2);
      fputs ("{\n", f);
      json_indent (f, indent + 3);
      fputs ("\"id\": ", f);
      json_write_prefixed_id (f, "is", (unsigned int) (i + 1));
      fputs (",\n", f);
      json_indent (f, indent + 3);
      fputs ("\"name\": ", f);
      json_write_string_or_null (f, sec != NULL ? sec->name : NULL);
      fputs (",\n", f);
      json_indent (f, indent + 3);
      fputs ("\"discarded\": ", f);
      fputs (vec->items[i].discarded ? "true,\n" : "false,\n", f);
      json_indent (f, indent + 3);
      fputs ("\"output_section_id\": ", f);
      if (outsec_for_id != NULL)
        {
          char idbuf[16];
          if (json_find_output_section_id (outsec_for_id, idbuf, sizeof (idbuf)))
            json_write_escaped_string (f, idbuf);
          else
            fputs ("null", f);
        }
      else
        fputs ("null", f);
      fputs (",\n", f);
      json_indent (f, indent + 3);
      fputs ("\"owner_file\": ", f);
      json_write_string_or_null (f,
                                 sec != NULL && sec->owner != NULL
                                 ? bfd_get_filename (sec->owner) : NULL);
      fputs (",\n", f);
      json_indent (f, indent + 3);
      fputs ("\"value_hex\": ", f);
      if (outsec != NULL)
        json_write_hex_vma (f, outsec->vma + sec->output_offset);
      else
        fputs ("null", f);
      fputs (",\n", f);
      json_indent (f, indent + 3);
      fputs ("\"size_hex\": ", f);
      if (sec != NULL)
        json_write_hex_vma (f, TO_ADDR (sec->size));
      else
        fputs ("null", f);
      fputs (",\n", f);
      json_indent (f, indent + 3);
      fputs ("\"output_offset_hex\": ", f);
      if (outsec != NULL)
        json_write_hex_vma (f, sec->output_offset);
      else
        fputs ("null", f);
      fputs (",\n", f);
      json_indent (f, indent + 3);
      fputs ("\"flags\": ", f);
      if (sec != NULL)
        json_write_flags_object (f, sec->flags, indent + 3);
      else
        fputs ("null", f);
      fputc ('\n', f);
      json_indent (f, indent + 2);
      fputc ('}', f);
    }
  if (!first_item)
    {
      fputc ('\n', f);
      json_indent (f, indent + 1);
    }
  fputs ("],\n", f);

  json_indent (f, indent + 1);
  fputs ("\"by_name\": ", f);
  for (i = 0; i < vec->count; i++)
    {
      size_t j;
      bool first_id = true;
      const char *name = vec->items[i].section != NULL
                         ? vec->items[i].section->name : NULL;

      if (name == NULL || json_input_section_name_seen_before (vec, i, name))
        continue;

      if (first_name)
        {
          fputs ("{\n", f);
          first_name = false;
        }
      else
        fputs (",\n", f);

      json_indent (f, indent + 2);
      json_write_escaped_string (f, name);
      fputs (": [", f);
      for (j = 0; j < vec->count; j++)
        {
          const char *other_name = vec->items[j].section != NULL
                                   ? vec->items[j].section->name : NULL;
          if (!json_strings_equal (name, other_name))
            continue;
          if (!first_id)
            fputs (", ", f);
          first_id = false;
          json_write_prefixed_id (f, "is", (unsigned int) (j + 1));
        }
      fputc (']', f);
    }
  if (first_name)
    fputs ("{},\n", f);
  else
    {
      fputc ('\n', f);
      json_indent (f, indent + 1);
      fputs ("},\n", f);
    }

  json_indent (f, indent + 1);
  fputs ("\"null_name_ids\": [", f);
  for (i = 0; i < vec->count; i++)
    {
      const char *name = vec->items[i].section != NULL
                         ? vec->items[i].section->name : NULL;
      if (name != NULL)
        continue;
      if (!first_null)
        fputs (", ", f);
      first_null = false;
      json_write_prefixed_id (f, "is", (unsigned int) (i + 1));
    }
  fputs ("]\n", f);

  json_indent (f, indent);
  fputc ('}', f);
}

static void
json_dump_discarded_input_sections (FILE *f,
                                    unsigned int indent,
                                    const struct json_input_section_vec *vec)
{
  size_t i;
  bool first_item = true;

  fputc ('[', f);
  for (i = 0; i < vec->count; i++)
    if (vec->items[i].discarded)
      {
        if (first_item)
          {
            fputc ('\n', f);
            first_item = false;
          }
        else
          fputs (",\n", f);

        json_indent (f, indent + 1);
        fputs ("{\n", f);
        json_indent (f, indent + 2);
        fputs ("\"input_section_id\": ", f);
        json_write_prefixed_id (f, "is", (unsigned int) (i + 1));
        fputs (",\n", f);
        json_indent (f, indent + 2);
        fputs ("\"discard_reason\": ", f);
        json_write_escaped_string (f, vec->items[i].discard_reason != NULL
                                      ? vec->items[i].discard_reason
                                      : "discarded");
        fputc ('\n', f);
        json_indent (f, indent + 1);
        fputc ('}', f);
      }

  if (!first_item)
    {
      fputc ('\n', f);
      json_indent (f, indent);
    }
  fputc (']', f);
}

static bool
json_region_name_seen_before (lang_memory_region_type *stop, const char *name)
{
  lang_memory_region_type *it;

  for (it = lang_memory_region_list; it != stop; it = it->next)
    if (json_strings_equal (it->name_list.name, name))
      return true;
  return false;
}

static void
json_dump_core_regions (FILE *f, unsigned int indent)
{
  lang_memory_region_type *m;
  bool first_item = true;
  bool first_name = true;
  bool first_null = true;
  unsigned int idx = 0;

  fputs ("{\n", f);

  json_indent (f, indent + 1);
  fputs ("\"items\": [", f);
  for (m = lang_memory_region_list; m != NULL; m = m->next)
    {
      char attrs_flags[16];
      char not_attrs_flags[16];

      idx++;
      json_flag_letters (m->flags, attrs_flags, sizeof (attrs_flags));
      json_flag_letters (m->not_flags, not_attrs_flags, sizeof (not_attrs_flags));

      if (first_item)
	{
	  fputc ('\n', f);
	  first_item = false;
	}
      else
	fputs (",\n", f);

      json_indent (f, indent + 2);
      fputs ("{\n", f);
      json_indent (f, indent + 3);
      fputs ("\"id\": ", f);
      json_write_id (f, 'r', idx);
      fputs (",\n", f);
      json_indent (f, indent + 3);
      fputs ("\"name\": ", f);
      json_write_string_or_null (f, m->name_list.name);
      fputs (",\n", f);
      json_indent (f, indent + 3);
      fputs ("\"origin_hex\": ", f);
      json_write_hex_vma (f, m->origin);
      fputs (",\n", f);
      json_indent (f, indent + 3);
      fputs ("\"length_hex\": ", f);
      json_write_hex_vma (f, m->length);
      fputs (",\n", f);
      json_indent (f, indent + 3);
      fputs ("\"attrs\": ", f);
      fputs ("{\n", f);
      json_indent (f, indent + 4);
      fputs ("\"required\": ", f);
      json_write_escaped_string (f, attrs_flags);
      fputs (",\n", f);
      json_indent (f, indent + 4);
      fputs ("\"forbidden\": ", f);
      json_write_escaped_string (f, not_attrs_flags);
      fputs (",\n", f);
      json_indent (f, indent + 4);
      fputs ("\"flags_hex\": ", f);
      json_write_hex_vma (f, (bfd_vma) m->flags);
      fputs (",\n", f);
      json_indent (f, indent + 4);
      fputs ("\"not_flags_hex\": ", f);
      json_write_hex_vma (f, (bfd_vma) m->not_flags);
      fputc ('\n', f);
      json_indent (f, indent + 3);
      fputc ('}', f);
      fputc ('\n', f);
      json_indent (f, indent + 2);
      fputc ('}', f);
    }
  if (!first_item)
    {
      fputc ('\n', f);
      json_indent (f, indent + 1);
    }
  fputs ("],\n", f);

  json_indent (f, indent + 1);
  fputs ("\"by_name\": ", f);
  for (m = lang_memory_region_list; m != NULL; m = m->next)
    {
      lang_memory_region_type *m2;
      unsigned int idx2 = 0;
      bool first_id = true;

      if (m->name_list.name == NULL
	  || json_region_name_seen_before (m, m->name_list.name))
	continue;

      if (first_name)
	{
	  fputs ("{\n", f);
	  first_name = false;
	}
      else
	fputs (",\n", f);

      json_indent (f, indent + 2);
      json_write_escaped_string (f, m->name_list.name);
      fputs (": [", f);

      for (m2 = lang_memory_region_list; m2 != NULL; m2 = m2->next)
	{
	  idx2++;
	  if (!json_strings_equal (m2->name_list.name, m->name_list.name))
	    continue;

	  if (first_id)
	    first_id = false;
	  else
	    fputs (", ", f);
	  json_write_id (f, 'r', idx2);
	}
      fputc (']', f);
    }
  if (first_name)
    fputs ("{},\n", f);
  else
    {
      fputc ('\n', f);
      json_indent (f, indent + 1);
      fputs ("},\n", f);
    }

  json_indent (f, indent + 1);
  fputs ("\"null_name_ids\": [", f);
  idx = 0;
  for (m = lang_memory_region_list; m != NULL; m = m->next)
    {
      idx++;
      if (m->name_list.name != NULL)
	continue;

      if (first_null)
	first_null = false;
      else
	fputs (", ", f);
      json_write_id (f, 'r', idx);
    }
  fputs ("]\n", f);

  json_indent (f, indent);
  fputc ('}', f);
}

static bool
json_section_name_seen_before (lang_output_section_statement_type *stop,
			       const char *name)
{
  lang_output_section_statement_type *it;

  for (it = (void *) lang_os_list.head; it != stop; it = it->next)
    if (json_strings_equal (it->name, name))
      return true;
  return false;
}

static void
json_dump_core_sections (FILE *f, unsigned int indent)
{
  lang_output_section_statement_type *os;
  bool first_item = true;
  bool first_name = true;
  bool first_null = true;
  unsigned int idx = 0;

  fputs ("{\n", f);

  json_indent (f, indent + 1);
  fputs ("\"items\": [", f);
  for (os = (void *) lang_os_list.head; os != NULL; os = os->next)
    {
      asection *outsec = os->bfd_section;

      idx++;

      if (first_item)
	{
	  fputc ('\n', f);
	  first_item = false;
	}
      else
	fputs (",\n", f);

      json_indent (f, indent + 2);
      fputs ("{\n", f);
      json_indent (f, indent + 3);
      fputs ("\"id\": ", f);
      json_write_id (f, 's', idx);
      fputs (",\n", f);
      json_indent (f, indent + 3);
      fputs ("\"name\": ", f);
      json_write_string_or_null (f, os->name);
      fputs (",\n", f);
      json_indent (f, indent + 3);
      fputs ("\"vma_region\": ", f);
      json_write_string_or_null (f,
				 os->region != NULL
				 ? os->region->name_list.name : NULL);
      fputs (",\n", f);
      json_indent (f, indent + 3);
      fputs ("\"lma_region\": ", f);
      json_write_string_or_null (f,
				 os->lma_region != NULL
				 ? os->lma_region->name_list.name : NULL);
      fputs (",\n", f);
      json_indent (f, indent + 3);
      fputs ("\"vma_hex\": ", f);
      if (outsec != NULL)
        json_write_hex_vma (f, outsec->vma);
      else
        fputs ("null", f);
      fputs (",\n", f);
      json_indent (f, indent + 3);
      fputs ("\"lma_hex\": ", f);
      if (outsec != NULL)
        json_write_hex_vma (f, outsec->lma);
      else
        fputs ("null", f);
      fputs (",\n", f);
      json_indent (f, indent + 3);
      fputs ("\"size_hex\": ", f);
      if (outsec != NULL)
        json_write_hex_vma (f, TO_ADDR (outsec->size));
      else
        fputs ("null", f);
      fputs (",\n", f);
      if (outsec != NULL)
        {
          json_indent (f, indent + 3);
          fputs ("\"flags\": ", f);
          json_write_flags_object (f, outsec->flags, indent + 3);
          fputs (",\n", f);
        }
      json_indent (f, indent + 3);
      fputs ("\"script_subsections\": ", f);
      json_dump_output_section_subsections (f, os, indent + 3);
      fputc ('\n', f);
      json_indent (f, indent + 2);
      fputc ('}', f);
    }
  if (!first_item)
    {
      fputc ('\n', f);
      json_indent (f, indent + 1);
    }
  fputs ("],\n", f);

  json_indent (f, indent + 1);
  fputs ("\"by_name\": ", f);
  for (os = (void *) lang_os_list.head; os != NULL; os = os->next)
    {
      lang_output_section_statement_type *os2;
      unsigned int idx2 = 0;
      bool first_id = true;

      if (os->name == NULL || json_section_name_seen_before (os, os->name))
	continue;

      if (first_name)
	{
	  fputs ("{\n", f);
	  first_name = false;
	}
      else
	fputs (",\n", f);

      json_indent (f, indent + 2);
      json_write_escaped_string (f, os->name);
      fputs (": [", f);
      for (os2 = (void *) lang_os_list.head; os2 != NULL; os2 = os2->next)
	{
	  idx2++;
	  if (!json_strings_equal (os2->name, os->name))
	    continue;

	  if (first_id)
	    first_id = false;
	  else
	    fputs (", ", f);
	  json_write_id (f, 's', idx2);
	}
      fputc (']', f);
    }
  if (first_name)
    fputs ("{},\n", f);
  else
    {
      fputc ('\n', f);
      json_indent (f, indent + 1);
      fputs ("},\n", f);
    }

  json_indent (f, indent + 1);
  fputs ("\"null_name_ids\": [", f);
  idx = 0;
  for (os = (void *) lang_os_list.head; os != NULL; os = os->next)
    {
      idx++;
      if (os->name != NULL)
	continue;

      if (first_null)
	first_null = false;
      else
	fputs (", ", f);
      json_write_id (f, 's', idx);
    }
  fputs ("]\n", f);

  json_indent (f, indent);
  fputc ('}', f);
}

static void json_dump_symbols (FILE *f,
                               unsigned int indent,
                               const struct json_input_section_vec *input_sections);
static void json_dump_core_symbols (FILE *f,
                                    unsigned int indent,
                                    const struct json_input_section_vec *input_sections);

static void
lang_dump_script_json (void)
{
  FILE *f;
  char *tmp_path = NULL;
  bool close_file = false;
  bool needs_install = false;
  struct json_input_section_vec input_sections;

  if (command_line.dump_script_json == NULL)
    return;

  if (command_line.dump_script_json[0] == '\0')
    {
      einfo (_("%P: empty argument passed to --dump-script-json; ignored\n"));
      return;
    }

  if (strcmp (command_line.dump_script_json, "-") == 0)
    f = stdout;
  else
    {
      tmp_path = concat (command_line.dump_script_json, ".tmp",
			 (const char *) NULL);
      f = fopen (tmp_path, FOPEN_WT);
      if (f == NULL)
	{
	  free (tmp_path);
	  einfo (_("%F%P: cannot open JSON dump file %s: %E\n"),
		 command_line.dump_script_json);
	}
      close_file = true;
      needs_install = true;
    }

  json_collect_input_sections (&input_sections);

  fputs ("{\n", f);
  json_indent (f, 1);
  fputs ("\"format\": {\n", f);
  json_indent (f, 2);
  fputs ("\"name\": ", f);
  json_write_escaped_string (f, LDJSON_FORMAT_NAME);
  fputs (",\n", f);
  json_indent (f, 2);
  fputs ("\"major\": ", f);
  fprintf (f, "%d,\n", LDJSON_FORMAT_MAJOR);
  json_indent (f, 2);
  fputs ("\"minor\": ", f);
  fprintf (f, "%d\n", LDJSON_FORMAT_MINOR);
  json_indent (f, 1);
  fputs ("},\n", f);
  json_indent (f, 1);
  fputs ("\"output\": ", f);
  json_dump_output_info (f, 1);
  fputs (",\n", f);

  json_indent (f, 1);
  fputs ("\"memory_regions\": ", f);
  json_dump_memory_regions (f, 1);
  fputs (",\n", f);

  json_indent (f, 1);
  fputs ("\"output_sections\": ", f);
  json_dump_output_sections (f, 1);
  fputs (",\n", f);

  json_indent (f, 1);
  fputs ("\"input_sections\": ", f);
  json_dump_input_sections (f, 1, &input_sections);
  fputs (",\n", f);

  json_indent (f, 1);
  fputs ("\"discarded_input_sections\": ", f);
  json_dump_discarded_input_sections (f, 1, &input_sections);
  fputs (",\n", f);

  json_indent (f, 1);
  fputs ("\"symbols\": ", f);
  json_dump_symbols (f, 1, &input_sections);
  fputs ("\n", f);
  fputs ("}\n", f);

  if (close_file)
    {
      int saved_errno = 0;
      bool io_failed = false;

      if (ferror (f))
	{
	  io_failed = true;
	  if (errno != 0)
	    saved_errno = errno;
	}

      if (fclose (f) != 0)
	{
	  io_failed = true;
	  if (saved_errno == 0)
	    saved_errno = errno;
	}

      if (io_failed)
	{
	  if (tmp_path != NULL)
	    unlink_if_ordinary (tmp_path);
	  free (tmp_path);
	  if (saved_errno != 0)
	    {
	      errno = saved_errno;
	      einfo (_("%F%P: failed to finalize JSON dump file %s: %E\n"),
		     command_line.dump_script_json);
	    }
	  einfo (_("%F%P: failed while writing JSON dump file %s\n"),
		 command_line.dump_script_json);
	}

      if (needs_install)
	{
	  unlink_if_ordinary (command_line.dump_script_json);

	  if (rename (tmp_path, command_line.dump_script_json) != 0)
	    {
	      int rename_errno = errno;

	      unlink_if_ordinary (tmp_path);
	      free (tmp_path);
	      errno = rename_errno;
	      einfo (_("%F%P: failed to install JSON dump file %s: %E\n"),
		     command_line.dump_script_json);
	    }
	}
    }
  else
    {
      if (fflush (f) != 0)
	einfo (_("%F%P: failed to flush JSON dump output %s: %E\n"),
	       command_line.dump_script_json);
      if (ferror (f))
	einfo (_("%F%P: failed while writing JSON dump output %s\n"),
	       command_line.dump_script_json);
    }

  json_free_input_sections (&input_sections);
  free (tmp_path);
}

/* ==== symbols collection helpers ==== */
struct json_symbol_vec
{
  struct bfd_link_hash_entry **items;
  size_t count;
  size_t cap;
};

static bool
json_symbol_is_dumpable (const struct bfd_link_hash_entry *h)
{
  return (h != NULL
          && h->root.string != NULL
          && h->root.string[0] != '\0'
          && LDJSON_SYM_IS_DUMPABLE (h));
}

static bool
json_collect_symbol_cb (struct bfd_link_hash_entry *h, void *data)
{
  struct json_symbol_vec *vec = (struct json_symbol_vec *) data;

  if (h == NULL || h->root.string == NULL)
    return true;
  if (!json_symbol_is_dumpable (h))
    return true;

  if (vec->count == vec->cap)
    {
      size_t new_cap = vec->cap == 0 ? 128 : vec->cap * 2;
      vec->items = (struct bfd_link_hash_entry **)
	xrealloc (vec->items, new_cap * sizeof (*vec->items));
      vec->cap = new_cap;
    }

  vec->items[vec->count++] = h;
  return true;
}

static int
json_cmp_symbol_ptrs (const void *a, const void *b)
{
  const struct bfd_link_hash_entry *sa =
    *(const struct bfd_link_hash_entry * const *) a;
  const struct bfd_link_hash_entry *sb =
    *(const struct bfd_link_hash_entry * const *) b;
  const char *na = sa->root.string != NULL ? sa->root.string : "";
  const char *nb = sb->root.string != NULL ? sb->root.string : "";
  return strcmp (na, nb);
}

static void
json_collect_sorted_symbols (struct json_symbol_vec *vec)
{
  vec->items = NULL;
  vec->count = 0;
  vec->cap = 0;

  bfd_link_hash_traverse (link_info.hash, json_collect_symbol_cb, vec);
  if (vec->count > 1)
    qsort (vec->items, vec->count, sizeof (*vec->items), json_cmp_symbol_ptrs);
}

static void
json_symbol_state_name (FILE *f, const struct bfd_link_hash_entry *h)
{
  const char *state = "defined";

  switch (h->type)
    {
    case bfd_link_hash_defined:
    case bfd_link_hash_defweak:
      state = "defined";
      break;
    case bfd_link_hash_undefined:
    case bfd_link_hash_undefweak:
      state = "undefined";
      break;
    case bfd_link_hash_common:
      state = "common";
      break;
    case bfd_link_hash_indirect:
      state = "alias";
      break;
    case bfd_link_hash_warning:
      state = "warning";
      break;
    default:
      state = "undefined";
      break;
    }

  json_write_escaped_string (f, state);
}

static asection *
json_symbol_section (struct bfd_link_hash_entry *h)
{
  switch (h->type)
    {
    case bfd_link_hash_defined:
    case bfd_link_hash_defweak:
      return h->u.def.section;
    case bfd_link_hash_common:
      return h->u.c.p != NULL ? h->u.c.p->section : NULL;
    default:
      return NULL;
    }
}

static bool
json_symbol_resolved_value (struct bfd_link_hash_entry *h,
                            asection *sec,
                            bfd_vma *value_out)
{
  asection *outsec;

  if (value_out == NULL)
    return false;

  if ((h->type != bfd_link_hash_defined && h->type != bfd_link_hash_defweak)
      || sec == NULL)
    return false;

  if (bfd_is_abs_section (sec))
    {
      *value_out = LDJSON_SYM_VALUE (h);
      return true;
    }

  outsec = json_get_final_output_section (sec);
  if (outsec != NULL)
    {
      bfd_vma offset = (sec == outsec) ? 0 : sec->output_offset;
      *value_out = LDJSON_SYM_VALUE (h) + outsec->vma + offset;
      return true;
    }

  return false;
}

static const char *
json_symbol_section_name (struct bfd_link_hash_entry *h, asection *sec)
{
  asection *outsec;

  if (sec == NULL)
    return NULL;
  if (bfd_is_abs_section (sec))
    return "ABS";

  outsec = json_get_final_output_section (sec);
  if (outsec != NULL && outsec->name != NULL)
    return outsec->name;

  if (h->type == bfd_link_hash_common && sec->name == NULL)
    return "COMMON";

  return sec->name;
}

static void
json_dump_one_symbol (FILE *f,
		      struct bfd_link_hash_entry *h,
		      unsigned int indent,
		      const char *stable_id,
                      const struct json_input_section_vec *input_sections)
{
  asection *sec = json_symbol_section (h);
  asection *outsec = json_get_final_output_section (sec);
  bool has_value = false;
  bfd_vma resolved_value = 0;
  bool force_abs_section = false;
  const char *section_name;

  has_value = json_symbol_resolved_value (h, sec, &resolved_value);
  if (h->ldscript_def && has_value && outsec != NULL
      && !json_output_section_contains_value (outsec, resolved_value))
    {
      asection *better_outsec = json_find_output_section_for_value (resolved_value);

      if (better_outsec != NULL)
        outsec = better_outsec;
      else
        {
          outsec = NULL;
          force_abs_section = true;
        }
    }

  if (force_abs_section)
    section_name = "ABS";
  else if (outsec != NULL && outsec->name != NULL)
    section_name = outsec->name;
  else
    section_name = json_symbol_section_name (h, sec);

  fputs ("{\n", f);
  if (stable_id != NULL)
    {
      json_indent (f, indent + 1);
      fputs ("\"id\": ", f);
      json_write_escaped_string (f, stable_id);
      fputs (",\n", f);
    }

  json_indent (f, indent + 1);
  fputs ("\"name\": ", f);
  json_write_escaped_string (f, h->root.string);
  fputs (",\n", f);

  json_indent (f, indent + 1);
  fputs ("\"state\": ", f);
  json_symbol_state_name (f, h);
  fputs (",\n", f);

  json_indent (f, indent + 1);
  fputs ("\"value_hex\": ", f);
  if (has_value)
    json_write_hex_vma (f, resolved_value);
  else
    fputs ("null", f);
  fputs (",\n", f);

  json_indent (f, indent + 1);
  fputs ("\"size_hex\": ", f);
  if (h->type == bfd_link_hash_common)
    json_write_hex_vma (f, TO_ADDR (h->u.c.size));
  else
    fputs ("null", f);
  fputs (",\n", f);

  json_indent (f, indent + 1);
  fputs ("\"section\": ", f);
  json_write_string_or_null (f, section_name);
  fputs (",\n", f);

  json_indent (f, indent + 1);
  fputs ("\"output_section_id\": ", f);
  if (outsec != NULL)
    {
      char idbuf[16];
      if (json_find_output_section_id (outsec, idbuf, sizeof (idbuf)))
        json_write_escaped_string (f, idbuf);
      else
        fputs ("null", f);
    }
  else
    fputs ("null", f);
  fputs (",\n", f);

  json_indent (f, indent + 1);
  fputs ("\"input_section_id\": ", f);
  if (json_find_input_section_id (input_sections, sec, NULL, 0))
    {
      char idbuf[16];
      json_find_input_section_id (input_sections, sec, idbuf, sizeof (idbuf));
      json_write_escaped_string (f, idbuf);
    }
  else
    fputs ("null", f);
  fputs (",\n", f);

  json_indent (f, indent + 1);
  fputs ("\"script_defined\": ", f);
  fputs (h->ldscript_def ? "true" : "false", f);
  fputc ('\n', f);

  json_indent (f, indent);
  fputc ('}', f);
}

static void
json_dump_symbols (FILE *f,
                   unsigned int indent,
                   const struct json_input_section_vec *input_sections)
{
  json_dump_core_symbols (f, indent, input_sections);
}

static void
json_dump_core_symbols (FILE *f,
                        unsigned int indent,
                        const struct json_input_section_vec *input_sections)
{
  struct json_symbol_vec vec;
  size_t i;
  bool first_item = true;

  json_collect_sorted_symbols (&vec);

  fputs ("{\n", f);

  json_indent (f, indent + 1);
  fputs ("\"items\": [", f);
  for (i = 0; i < vec.count; i++)
    {
      char idbuf[16];

      if (first_item)
	{
	  fputc ('\n', f);
	  first_item = false;
	}
      else
	fputs (",\n", f);

      sprintf (idbuf, "v%06u", (unsigned int) (i + 1));
      json_indent (f, indent + 2);
      json_dump_one_symbol (f, vec.items[i], indent + 2, idbuf, input_sections);
    }
  if (!first_item)
    {
      fputc ('\n', f);
      json_indent (f, indent + 1);
    }
  fputs ("],\n", f);

  json_indent (f, indent + 1);
  fputs ("\"by_name\": ", f);
  if (vec.count == 0)
    fputs ("{},\n", f);
  else
    {
      size_t start = 0;
      bool first_group = true;

      fputs ("{\n", f);
      while (start < vec.count)
	{
	  size_t end = start + 1;
	  const char *name = vec.items[start]->root.string;

	  while (end < vec.count
		 && strcmp (vec.items[end]->root.string, name) == 0)
	    end++;

	  if (!first_group)
	    fputs (",\n", f);
	  first_group = false;

	  json_indent (f, indent + 2);
	  json_write_escaped_string (f, name);
	  fputs (": [", f);
	  for (i = start; i < end; i++)
	    {
	      if (i != start)
		fputs (", ", f);
	      json_write_id (f, 'v', (unsigned int) (i + 1));
	    }
	  fputc (']', f);

	  start = end;
	}
      fputc ('\n', f);
      json_indent (f, indent + 1);
      fputs ("},\n", f);
    }

  json_indent (f, indent + 1);
  fputs ("\"null_name_ids\": []\n", f);
  json_indent (f, indent);
  fputc ('}', f);

  free (vec.items);
}

/* Integration reminders:
 * 1) Wire option via ldjson_options.def in ld.h/ldlex.h/lexsup.c.
 * 2) Call lang_dump_script_json() at end of lang_process() after lang_end().
 * 3) Keep ldscript_json_impl.inc included in file scope inside ldlang.c.
 * 4) Indexed top-level collections: memory_regions/output_sections/input_sections/symbols.
 * 5) discarded_input_sections is a plain top-level list of discard records.
 * 6) by_name maps are duplicate-safe (name -> [id...]).
 */
