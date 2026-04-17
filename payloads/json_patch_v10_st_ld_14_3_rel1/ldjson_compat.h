#ifndef LDJSON_COMPAT_H
#define LDJSON_COMPAT_H

/*
 * Compatibility shim for small API/field differences across ld versions.
 * These macros do not change JSON schema shape; they only adapt to local
 * ld/BFD struct layouts and expose the current format version constants.
 */

/* Stable format metadata emitted by lang_dump_script_json(). */
#ifndef LDJSON_FORMAT_NAME
#define LDJSON_FORMAT_NAME "ldscript-json"
#endif

#ifndef LDJSON_FORMAT_MAJOR
#define LDJSON_FORMAT_MAJOR 10
#endif

#ifndef LDJSON_FORMAT_MINOR
#define LDJSON_FORMAT_MINOR 0
#endif

#ifndef LDJSON_SYM_IS_DUMPABLE
#define LDJSON_SYM_IS_DUMPABLE(H) \
  ((H) != NULL && (H)->type != bfd_link_hash_new)
#endif

#ifndef LDJSON_SYM_SECTION
#define LDJSON_SYM_SECTION(H) ((H)->u.def.section)
#endif

#ifndef LDJSON_SYM_VALUE
#define LDJSON_SYM_VALUE(H) ((H)->u.def.value)
#endif

#ifndef LDJSON_SEC_IS_OUTPUT_MAPPED
#define LDJSON_SEC_IS_OUTPUT_MAPPED(SEC) \
  ((SEC) != NULL \
   && (SEC)->output_section != NULL \
   && (SEC)->output_section->owner == link_info.output_bfd)
#endif

#endif /* LDJSON_COMPAT_H */
