  default:
    if (reglex_read_ahead.length == 0) {
      reglex_parse_result = 0;
    } else {
      reglex_parse_result = 1;
    }
    break;
  }
  reglex_checkpoint_tag = -1;
  reglex_clear_str(&reglex_lexem_str);
  reglex_read_ahead_ptr = reglex_read_ahead.length;
}

int reglex_next() {
  int c;
  if (reglex_read_ahead_ptr > 0) {
    c = reglex_read_ahead.data[reglex_read_ahead.length - reglex_read_ahead_ptr--];
  } else {
    c = fgetc(stdin);
    if (c != EOF) {
      reglex_append_char_to_str(&reglex_read_ahead, c);
    }
  }
  return c;
}

int reglex_parse() {
  while (reglex_parse_result == -1) {
    reglex_parse_token();
  }
  return reglex_parse_result;
}
