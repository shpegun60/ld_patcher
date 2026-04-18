# validate_core.jq
# Usage:
#   jq -e -f ld_patcher/docs/reference_samples/validate_core.jq out.json

def chk($cond; $msg):
  if $cond then [] else [$msg] end;

def is_obj: type == "object";
def is_arr: type == "array";
def is_bool: type == "boolean";
def is_str: type == "string";
def is_str_or_null: (type == "string" or . == null);
def is_id_array: is_arr and all(.[]; type == "string");

def is_by_name_index:
  if type != "object" then
    false
  else
    all(to_entries[]; (.value | is_id_array))
  end;

def has_indexed_block_shape:
  (type == "object")
  and (.items | is_arr)
  and (.by_name | is_by_name_index)
  and (.null_name_ids | is_id_array);

def has_id_and_name:
  (type == "object")
  and (.id? | is_str)
  and (.name? | is_str_or_null);

def flags_obj_ok:
  (type == "object")
  and (.letters? | is_str)
  and (.hex? | is_str);

def attrs_obj_ok:
  (type == "object")
  and (.required? | is_str)
  and (.forbidden? | is_str)
  and (.flags_hex? | is_str)
  and (.not_flags_hex? | is_str);

def memory_region_item_ok:
  has_id_and_name
  and (.origin_hex? | is_str)
  and (.length_hex? | is_str)
  and (.attrs? | attrs_obj_ok);

def script_subsections_ok:
  (.script_subsections? | is_arr and all(.[]; type == "string"));

def output_section_item_ok:
  has_id_and_name
  and (.vma_region? | is_str_or_null)
  and (.lma_region? | is_str_or_null)
  and (.vma_hex? | is_str_or_null)
  and (.lma_hex? | is_str_or_null)
  and (.size_hex? | is_str_or_null)
  and script_subsections_ok
  and ((has("flags") | not) or (.flags | flags_obj_ok))
  and ((has("region") | not))
  and ((has("bfd_section") | not));

def input_section_item_ok:
  has_id_and_name
  and (.discarded? | is_bool)
  and (.output_section_id? | is_str_or_null)
  and (.owner_file? | is_str_or_null)
  and (.value_hex? | is_str_or_null)
  and (.size_hex? | is_str_or_null)
  and (.output_offset_hex? | is_str_or_null)
  and (.flags? | flags_obj_ok)
  and ((has("object_file") | not))
  and ((has("archive_file") | not))
  and ((has("archive_member") | not))
  and ((has("input_statement_file") | not))
  and ((has("rawsize_hex") | not));

def discard_record_ok:
  (type == "object")
  and (.input_section_id? | is_str)
  and (.discard_reason? | is_str);

def symbol_state_ok:
  (. == "defined"
   or . == "undefined"
   or . == "common"
   or . == "alias"
   or . == "warning");

def symbol_item_ok:
  has_id_and_name
  and (.state? | (type == "string" and symbol_state_ok))
  and (.value_hex? | is_str_or_null)
  and (.size_hex? | is_str_or_null)
  and (.section? | is_str_or_null)
  and (.output_section_id? | is_str_or_null)
  and (.input_section_id? | is_str_or_null)
  and (.script_defined? | is_bool)
  and ((has("hash_type") | not));

def discard_matches_input($root):
  ($root.input_sections.items // []) as $items
  | ($root.discarded_input_sections // []) as $discard
  | all($items[];
      . as $item
      | if $item.discarded == true then
          ([ $discard[] | select(.input_section_id == $item.id) ] | length) == 1
      else
          ([ $discard[] | select(.input_section_id == $item.id) ] | length) == 0
        end);

def discard_refs_exist($root):
  ($root.input_sections.items // [] | map(.id)) as $ids
  | all(($root.discarded_input_sections // [])[]; ($ids | index(.input_section_id)) != null);

def discard_flags_match($root):
  ($root.input_sections.items // []) as $items
  | all(($root.discarded_input_sections // [])[];
      . as $record
      | any($items[]; .id == $record.input_section_id and .discarded == true));

def checks:
  chk((type == "object"); "root must be object")

  + chk((.format? | is_obj); "missing format object")
  + chk((.format.name? == "ldscript-json"); "format.name must be ldscript-json")
  + chk((.format.major? | type) == "number"; "format.major must be number")
  + chk((.format.minor? | type) == "number"; "format.minor must be number")
  + chk((has("capabilities") | not); "capabilities is not part of the current format")
  + chk((has("schema_version") | not); "schema_version is not part of the current format")
  + chk((has("extensions") | not); "extensions is not part of the current format")
  + chk((has("script_variables") | not); "script_variables is not part of the current format")

  + chk((.output? | is_obj); "missing output object")
  + chk((.output.entry_symbol? | is_str_or_null); "output.entry_symbol must be string|null")
  + chk((.output.filename? == null); "output.filename is not part of the current format")
  + chk((.output.target? == null); "output.target is not part of the current format")
  + chk((.output.entry_from_cmdline? == null); "output.entry_from_cmdline is not part of the current format")
  + chk((.output.is_relocatable? == null); "output.is_relocatable is not part of the current format")
  + chk((.output.is_shared? == null); "output.is_shared is not part of the current format")
  + chk((.output.is_pie? == null); "output.is_pie is not part of the current format")

  + chk((.memory_regions? | has_indexed_block_shape); "memory_regions must have items/by_name/null_name_ids with id arrays")
  + chk((.memory_regions.items? | is_arr and all(.[]; memory_region_item_ok)); "memory_regions.items[] must contain id, name, origin_hex, length_hex, and attrs object")

  + chk((.output_sections? | has_indexed_block_shape); "output_sections must have items/by_name/null_name_ids with id arrays")
  + chk((.output_sections.items? | is_arr and all(.[]; output_section_item_ok)); "output_sections.items[] must contain id/name/vma_region/lma_region/vma_hex/lma_hex/size_hex/script_subsections")

  + chk((.input_sections? | has_indexed_block_shape); "input_sections must have items/by_name/null_name_ids with id arrays")
  + chk((.input_sections.items? | is_arr and all(.[]; input_section_item_ok)); "input_sections.items[] must contain id/name/discarded/output_section_id/owner_file/value_hex/size_hex/output_offset_hex/flags")

  + chk((.discarded_input_sections? | is_arr); "discarded_input_sections must be an array")
  + chk((.discarded_input_sections? | all(.[]; discard_record_ok)); "every discarded_input_sections[] entry must contain input_section_id and discard_reason")
  + chk(discard_refs_exist(.); "every discarded_input_sections[].input_section_id must exist in input_sections.items[]")
  + chk(discard_matches_input(.); "discarded_input_sections must exactly match input_sections.items[].discarded flags")
  + chk(discard_flags_match(.); "every discard record must reference an input section with discarded=true")

  + chk((.symbols? | has_indexed_block_shape); "symbols must have items/by_name/null_name_ids with id arrays")
  + chk((.symbols.items? | is_arr and all(.[]; symbol_item_ok)); "every symbols.items[] must contain id, name, state, value_hex, size_hex, section, output_section_id, input_section_id, and script_defined")
;

(checks) as $errors
| if ($errors | length) == 0 then
    { "ok": true, "message": "canonical contract is valid" }
  else
    error($errors | join("\n"))
  end
