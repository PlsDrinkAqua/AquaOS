/* Complete the final shell word using directory entries. */
static int completion_separator(char c) {
    return c == ' ' || c == '\t' || c == '|' || c == '<' || c == '>' || c == '&';
}

static void complete_filename(char *line, size_t capacity) {
    size_t length = strlen(line), start = length, base;
    char directory[256], name[256], common[256], path[256];
    unsigned matches = 0;
    size_t common_length = 0;
    while (start && !completion_separator(line[start - 1])) start--;
    base = start;
    for (size_t i = start; i < length; i++)
        if (line[i] == '/') base = i + 1;
    size_t directory_length = base - start;
    if (directory_length >= sizeof(directory)) return;
    if (directory_length) {
        memcpy(directory, line + start, directory_length);
        directory[directory_length] = '\0';
    } else strcpy(directory, ".");

    int fd = open(directory, O_RDONLY);
    if (fd < 0) return;
    size_t prefix_length = length - base;
    int result;
    while ((result = readdir(fd, name, sizeof(name))) > 0) {
        if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
        if (name[0] == '.' && line[base] != '.') continue;
        if (strncmp(name, line + base, prefix_length)) continue;
        /* These names cannot yet be represented by the shell parser. */
        int usable = 1;
        for (size_t i = 0; name[i]; i++)
            if (completion_separator(name[i]) || (unsigned char)name[i] < 32)
                usable = 0;
        if (!usable) continue;
        if (!matches++) {
            strcpy(common, name);
            common_length = strlen(common);
        } else {
            size_t i = 0;
            while (i < common_length && name[i] == common[i]) i++;
            common_length = i;
            common[i] = '\0';
        }
    }
    close(fd);
    if (result < 0 || !matches || base + common_length >= capacity) return;
    memcpy(line + base, common, common_length + 1);
    length = base + common_length;

    /* A unique directory gets a slash so another Tab can complete its children. */
    if (matches == 1 && length + 1 < capacity && length - start < sizeof(path)) {
        strcpy(path, line + start);
        fd = open(path, O_RDONLY);
        if (fd >= 0) {
            result = readdir(fd, name, sizeof(name));
            close(fd);
            if (result >= 0) {
                line[length++] = '/';
                line[length] = '\0';
            }
        }
    }
}
