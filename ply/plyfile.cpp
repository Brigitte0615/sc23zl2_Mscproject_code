#include "ply.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <string>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <cstring> 

#if defined(_MSC_VER)
#pragma warning(disable : 4996)
#endif

#ifdef WIN32
#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN
#endif
#endif

const char* type_names[] = { "invalid", "char", "short", "int", "uchar", "ushort", "uint", "float", "double", "float32", "uint8", "int32" };

int ply_type_size[] = { 0, 1, 2, 4, 1, 2, 4, 4, 8, 4, 1, 4 };

#define NO_OTHER_PROPS -1

#define DONT_STORE_PROP 0
#define STORE_PROP 1

#define OTHER_PROP 0
#define NAMED_PROP 1

int equal_strings(const char*, const char*);
PlyElement* find_element(PlyFile*, const char*);
PlyProperty* find_property(PlyElement*, const char*, int*);

void write_scalar_type(FILE*, int);
char** get_words(FILE*, int*, char**);
void write_binary_item(PlyFile*, int, unsigned int, double, int);
void write_ascii_item(FILE*, int, unsigned int, double, int);
void add_element(PlyFile*, char**, int);
void add_property(PlyFile*, char**, int);
void add_comment(PlyFile*, char*);
void add_obj_info(PlyFile*, char*);
void copy_property(PlyProperty*, PlyProperty*);
void store_item(char*, int, int, unsigned int, double);
void get_stored_item(void*, int, int*, unsigned int*, double*);
double get_item_value(char*, int);
void get_ascii_item(char*, int, int*, unsigned int*, double*);
void get_binary_item(PlyFile*, int, int*, unsigned int*, double*);
void ascii_get_element(PlyFile*, char*);
void binary_get_element(PlyFile*, char*);

char* my_alloc(int, int, const char*);


void swap2Bytes(void* ptr)
{
    unsigned char* bytes = (unsigned char*)ptr;
    unsigned short* result = (unsigned short*)ptr;

    *result = (bytes[0] << 8) | bytes[1];
}

void swap4Bytes(void* ptr)
{
    unsigned char* bytes = (unsigned char*)ptr;
    unsigned int* result = (unsigned int*)ptr;

    *result = (bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3];
}

void swap8Bytes(void* ptr)
{
    unsigned char* bytes = (unsigned char*)ptr;
    unsigned long long* result = (unsigned long long*)ptr;

    *result = ((unsigned long long)(bytes[0])) << 56 | ((unsigned long long)(bytes[1])) << 48 | ((unsigned long long)(bytes[2])) << 40 | ((unsigned long long)(bytes[3])) << 32 | ((unsigned long long)(bytes[4])) << 24 | ((unsigned long long)(bytes[5])) << 16 | ((unsigned long long)(bytes[6])) << 8 | bytes[7];
}

#ifdef LITTLE_ENDIAN
void swap2LE(void*)
{
}
void swap2LE(short*)
{
}
void swap2LE(unsigned short*)
{
}
void swap4LE(void*)
{
}
void swap4LE(int*)
{
}
void swap4LE(unsigned int*)
{
}
void swap4LE(float*)
{
}
void swap8LE(void*)
{
}
void swap8LE(long long*)
{
}
void swap8LE(unsigned long long*)
{
}
void swap8LE(double*)
{
}

void swap2BE(void* ptr)
{
    swap2Bytes(ptr);
}
void swap2BE(short* ptr)
{
    swap2Bytes(ptr);
}
void swap2BE(unsigned short* ptr)
{
    swap2Bytes(ptr);
}
void swap4BE(void* ptr)
{
    swap4Bytes(ptr);
}
void swap4BE(int* ptr)
{
    swap4Bytes(ptr);
}
void swap4BE(unsigned int* ptr)
{
    swap4Bytes(ptr);
}
void swap4BE(float* ptr)
{
    swap4Bytes(ptr);
}
void swap8BE(long long* ptr)
{
    swap8Bytes(ptr);
}
void swap8BE(void* ptr)
{
    swap8Bytes(ptr);
}
void swap8BE(unsigned long long* ptr)
{
    swap8Bytes(ptr);
}
void swap8BE(double* ptr)
{
    swap8Bytes(ptr);
}
#endif 
PlyFile* ply_write(FILE* fp, int nelems, const char** elem_names, int file_type)
{
    if (!fp) return nullptr;

    PlyFile* plyfile = (PlyFile*)myalloc(sizeof(PlyFile));  
    plyfile->file_type = file_type;
    plyfile->num_comments = 0;
    plyfile->num_obj_info = 0;
    plyfile->nelems = nelems;
    plyfile->version = 1.0;
    plyfile->fp = fp;
    plyfile->other_elems = nullptr;

    plyfile->elems = (PlyElement**)myalloc(sizeof(PlyElement*) * nelems); 
    for (int i = 0; i < nelems; ++i)
    {
        PlyElement* elem = (PlyElement*)myalloc(sizeof(PlyElement)); 
        elem->name = strdup(elem_names[i]);
        elem->num = 0;
        elem->nprops = 0;
        plyfile->elems[i] = elem;
    }

    return plyfile;
}

PlyFile* ply_open_for_writing(char* filename, int nelems, const char** elem_names, int file_type, float* version)
{
    FILE* fp = fopen(filename, "wb");
    if (!fp) return nullptr;

    PlyFile* plyfile = ply_write(fp, nelems, elem_names, file_type);
    if (!plyfile) return nullptr;

    *version = plyfile->version;
    return plyfile;
}

void ply_describe_element(PlyFile* plyfile, const char* elem_name, int nelems, int nprops, PlyProperty* prop_list)
{
    PlyElement* elem = find_element(plyfile, elem_name);
    if (!elem)
    {
        throw std::runtime_error(std::string("ply_describe_element: can't find element '") + elem_name + "'");
    }

    elem->num = nelems;
    elem->nprops = nprops;
    elem->props = (PlyProperty**)myalloc(sizeof(PlyProperty*) * nprops); 
    elem->store_prop = (char*)myalloc(sizeof(char) * nprops);  

    for (int i = 0; i < nprops; ++i)
    {
        PlyProperty* prop = (PlyProperty*)myalloc(sizeof(PlyProperty));  
        copy_property(prop, &prop_list[i]);
        elem->props[i] = prop;
        elem->store_prop[i] = NAMED_PROP;
    }
}

void ply_describe_property(PlyFile* plyfile, const char* elem_name, PlyProperty* prop)
{
    PlyElement* elem = find_element(plyfile, elem_name);
    if (!elem)
    {
        fprintf(stderr, "ply_describe_property: can't find element '%s'\n", elem_name);
        return;
    }

    int new_index = elem->nprops;
    elem->nprops += 1;

    elem->props = elem->nprops == 1
        ? (PlyProperty**)myalloc(sizeof(PlyProperty*))
        : (PlyProperty**)realloc(elem->props, sizeof(PlyProperty*) * elem->nprops);

    elem->store_prop = elem->nprops == 1
        ? (char*)myalloc(sizeof(char))
        : (char*)realloc(elem->store_prop, sizeof(char) * elem->nprops);

    elem->other_offset = 0;

    elem->props[new_index] = (PlyProperty*)myalloc(sizeof(PlyProperty));
    elem->store_prop[new_index] = NAMED_PROP;

    copy_property(elem->props[new_index], prop);
}

void ply_describe_other_properties(PlyFile* plyfile, PlyOtherProp* other, int offset)
{
    PlyElement* elem = find_element(plyfile, other->name);
    if (!elem)
    {
        fprintf(stderr, "ply_describe_other_properties: can't find element '%s'\n", other->name);
        return;
    }

    int new_props_count = elem->nprops + other->nprops;

    elem->props = elem->nprops == 0
        ? (PlyProperty**)myalloc(sizeof(PlyProperty*) * other->nprops)
        : (PlyProperty**)realloc(elem->props, sizeof(PlyProperty*) * new_props_count);

    elem->store_prop = elem->nprops == 0
        ? (char*)myalloc(sizeof(char) * other->nprops)
        : (char*)realloc(elem->store_prop, sizeof(char) * new_props_count);

    for (int i = 0; i < other->nprops; ++i)
    {
        PlyProperty* prop = (PlyProperty*)myalloc(sizeof(PlyProperty));
        copy_property(prop, other->props[i]);
        elem->props[elem->nprops] = prop;
        elem->store_prop[elem->nprops] = OTHER_PROP;
        elem->nprops++;
    }

    elem->other_size = other->size;
    elem->other_offset = offset;
}

void ply_element_count(PlyFile* plyfile, const char* elem_name, int nelems)
{
    PlyElement* elem = find_element(plyfile, elem_name);
    if (!elem)
    {
        char error[100];
        sprintf(error, "ply_element_count: can't find element '%s'\n", elem_name);
        throw std::runtime_error(error);
    }
    elem->num = nelems;
}

void ply_header_complete(PlyFile* plyfile)
{
    FILE* fp = plyfile->fp;
    fprintf(fp, "ply\n");

    switch (plyfile->file_type)
    {
    case PLY_ASCII:
        fprintf(fp, "format ascii 1.0\n");
        break;
    case PLY_BINARY_BE:
        fprintf(fp, "format binary_big_endian 1.0\n");
        break;
    case PLY_BINARY_LE:
        fprintf(fp, "format binary_little_endian 1.0\n");
        break;
    default:
        char error[100];
        sprintf(error, "ply_header_complete: bad file type = %d\n", plyfile->file_type);
        throw std::runtime_error(error);
    }

    for (int i = 0; i < plyfile->num_comments; ++i)
        fprintf(fp, "comment %s\n", plyfile->comments[i]);

    for (int i = 0; i < plyfile->num_obj_info; ++i)
        fprintf(fp, "obj_info %s\n", plyfile->obj_info[i]);

    for (int i = 0; i < plyfile->nelems; ++i)
    {
        PlyElement* elem = plyfile->elems[i];
        fprintf(fp, "element %s %d\n", elem->name, elem->num);

        for (int j = 0; j < elem->nprops; ++j)
        {
            PlyProperty* prop = elem->props[j];
            if (prop->is_list)
            {
                fprintf(fp, "property list ");
                write_scalar_type(fp, prop->count_external);
                fprintf(fp, " ");
                write_scalar_type(fp, prop->external_type);
                fprintf(fp, " %s\n", prop->name);
            }
            else
            {
                fprintf(fp, "property ");
                write_scalar_type(fp, prop->external_type);
                fprintf(fp, " %s\n", prop->name);
            }
        }
    }

    fprintf(fp, "end_header\n");
}

void ply_put_element_setup(PlyFile* plyfile, const char* elem_name)
{
    PlyElement* elem = find_element(plyfile, elem_name);
    if (!elem)
    {
        char error[100];
        sprintf(error, "ply_elements_setup: can't find element '%s'\n", elem_name);
        throw std::runtime_error(error);
    }

    plyfile->which_elem = elem;
}

void ply_put_element(PlyFile* plyfile, void* elem_ptr)
{
    PlyElement* elem = plyfile->which_elem;
    char* elem_data = (char*)elem_ptr;
    char** other_ptr = (char**)(((char*)elem_ptr) + elem->other_offset);

    if (plyfile->file_type == PLY_ASCII)
    {
        for (int j = 0; j < elem->nprops; ++j)
        {
            PlyProperty* prop = elem->props[j];
            elem_data = elem->store_prop[j] == OTHER_PROP ? *other_ptr : (char*)elem_ptr;

            if (prop->is_list)
            {
                char* item = elem_data + prop->count_offset;
                int int_val;
                unsigned int uint_val;
                double double_val;
                get_stored_item((void*)item, prop->count_internal, &int_val, &uint_val, &double_val);
                write_ascii_item(plyfile->fp, int_val, uint_val, double_val, prop->count_external);
                int list_count = uint_val;

                char** item_ptr = (char**)(elem_data + prop->offset);
                item = item_ptr[0];
                int item_size = ply_type_size[prop->internal_type];
                for (int k = 0; k < list_count; ++k)
                {
                    get_stored_item((void*)item, prop->internal_type, &int_val, &uint_val, &double_val);
                    write_ascii_item(plyfile->fp, int_val, uint_val, double_val, prop->external_type);
                    item += item_size;
                }
            }
            else
            {
                char* item = elem_data + prop->offset;
                int int_val;
                unsigned int uint_val;
                double double_val;
                get_stored_item((void*)item, prop->internal_type, &int_val, &uint_val, &double_val);
                write_ascii_item(plyfile->fp, int_val, uint_val, double_val, prop->external_type);
            }
        }
        fprintf(plyfile->fp, "\n");
    }
    else
    {
        for (int j = 0; j < elem->nprops; ++j)
        {
            PlyProperty* prop = elem->props[j];
            elem_data = elem->store_prop[j] == OTHER_PROP ? *other_ptr : (char*)elem_ptr;

            if (prop->is_list)
            {
                char* item = elem_data + prop->count_offset;
                int int_val;
                unsigned int uint_val;
                double double_val;
                int item_size = ply_type_size[prop->count_internal];
                get_stored_item((void*)item, prop->count_internal, &int_val, &uint_val, &double_val);
                write_binary_item(plyfile, int_val, uint_val, double_val, prop->count_external);
                int list_count = uint_val;

                char** item_ptr = (char**)(elem_data + prop->offset);
                item = item_ptr[0];
                item_size = ply_type_size[prop->internal_type];
                for (int k = 0; k < list_count; ++k)
                {
                    get_stored_item((void*)item, prop->internal_type, &int_val, &uint_val, &double_val);
                    write_binary_item(plyfile, int_val, uint_val, double_val, prop->external_type);
                    item += item_size;
                }
            }
            else
            {
                char* item = elem_data + prop->offset;
                int int_val;
                unsigned int uint_val;
                double double_val;
                get_stored_item((void*)item, prop->internal_type, &int_val, &uint_val, &double_val);
                write_binary_item(plyfile, int_val, uint_val, double_val, prop->external_type);
            }
        }
    }
}

void ply_put_comment(PlyFile* plyfile, const char* comment)
{
    int new_count = plyfile->num_comments + 1;
    plyfile->comments = plyfile->num_comments == 0
        ? (char**)myalloc(sizeof(char*))
        : (char**)realloc(plyfile->comments, sizeof(char*) * new_count);
    plyfile->comments[plyfile->num_comments] = strdup(comment);
    plyfile->num_comments = new_count;
}

void ply_put_obj_info(PlyFile* plyfile, const char* obj_info)
{
    int new_count = plyfile->num_obj_info + 1;
    plyfile->obj_info = plyfile->num_obj_info == 0
        ? (char**)myalloc(sizeof(char*))
        : (char**)realloc(plyfile->obj_info, sizeof(char*) * new_count);
    plyfile->obj_info[plyfile->num_obj_info] = strdup(obj_info);
    plyfile->num_obj_info = new_count;
}

PlyFile* ply_read(FILE* fp, int* nelems, char*** elem_names)
{
    int i, j;
    PlyFile* plyfile;
    int nwords;
    char** words;
    char** elist;
    PlyElement* elem;
    char* orig_line;

    if (fp == NULL)
        return (NULL);

    plyfile = (PlyFile*)myalloc(sizeof(PlyFile));
    if (!plyfile)
        return (NULL);

    plyfile->nelems = 0;
    plyfile->comments = NULL;
    plyfile->num_comments = 0;
    plyfile->obj_info = NULL;
    plyfile->num_obj_info = 0;
    plyfile->fp = fp;
    plyfile->other_elems = NULL;

    words = get_words(plyfile->fp, &nwords, &orig_line);
    if (!words || !equal_strings(words[0], "ply"))
    {
        if (words)
            free(words);
        free(plyfile);
        return (NULL);
    }

    while (words)
    {
        if (equal_strings(words[0], "format"))
        {
            if (nwords != 3)
            {
                free(words);
                free(plyfile);
                return (NULL);
            }
            if (equal_strings(words[1], "ascii"))
                plyfile->file_type = PLY_ASCII;
            else if (equal_strings(words[1], "binary_big_endian"))
                plyfile->file_type = PLY_BINARY_BE;
            else if (equal_strings(words[1], "binary_little_endian"))
                plyfile->file_type = PLY_BINARY_LE;
            else
            {
                free(words);
                free(plyfile);
                return (NULL);
            }
            plyfile->version = std::stod(words[2]);
        }
        else if (equal_strings(words[0], "element"))
            add_element(plyfile, words, nwords);
        else if (equal_strings(words[0], "property"))
            add_property(plyfile, words, nwords);
        else if (equal_strings(words[0], "comment"))
            add_comment(plyfile, orig_line);
        else if (equal_strings(words[0], "obj_info"))
            add_obj_info(plyfile, orig_line);
        else if (equal_strings(words[0], "end_header"))
        {
            free(words);
            break;
        }

        free(words);

        words = get_words(plyfile->fp, &nwords, &orig_line);
    }

    for (i = 0; i < plyfile->nelems; i++)
    {
        elem = plyfile->elems[i];
        elem->store_prop = (char*)myalloc(sizeof(char) * elem->nprops);
        for (j = 0; j < elem->nprops; j++)
            elem->store_prop[j] = DONT_STORE_PROP;
        elem->other_offset = NO_OTHER_PROPS; /* no "other" props by default */
    }

    elist = (char**)myalloc(sizeof(char*) * plyfile->nelems);
    for (i = 0; i < plyfile->nelems; i++)
        elist[i] = strdup(plyfile->elems[i]->name);

    *elem_names = elist;
    *nelems = plyfile->nelems;

    return (plyfile);
}

PlyFile* ply_open_for_reading(char* filename, int* nelems, char*** elem_names, int* file_type, float* version)
{
    FILE* fp = fopen(filename, "rb");
    if (!fp) return NULL;

    PlyFile* plyfile = ply_read(fp, nelems, elem_names);
    if (!plyfile) return NULL;

    *file_type = plyfile->file_type;
    *version = plyfile->version;

    return plyfile;
}

PlyProperty** ply_get_element_description(PlyFile* plyfile, char* elem_name, int* nelems, int* nprops)
{
    PlyElement* elem = find_element(plyfile, elem_name);
    if (!elem) return NULL;

    *nelems = elem->num;
    *nprops = elem->nprops;

    PlyProperty** prop_list = (PlyProperty**)myalloc(sizeof(PlyProperty*) * elem->nprops);
    for (int i = 0; i < elem->nprops; ++i)
    {
        prop_list[i] = (PlyProperty*)myalloc(sizeof(PlyProperty));
        copy_property(prop_list[i], elem->props[i]);
    }

    return prop_list;
}

void ply_get_element_setup(PlyFile* plyfile, char* elem_name, int nprops, PlyProperty* prop_list)
{
    PlyElement* elem = find_element(plyfile, elem_name);
    if (!elem)
    {
        fprintf(stderr, "Warning:  Can't find element '%s'\n", elem_name);
        return;
    }

    plyfile->which_elem = elem;

    for (int i = 0; i < nprops; ++i)
    {
        int index;
        PlyProperty* prop = find_property(elem, prop_list[i].name, &index);
        if (!prop)
        {
            fprintf(stderr, "Warning:  Can't find property '%s' in element '%s'\n", prop_list[i].name, elem_name);
            continue;
        }

        prop->internal_type = prop_list[i].internal_type;
        prop->offset = prop_list[i].offset;
        prop->count_internal = prop_list[i].count_internal;
        prop->count_offset = prop_list[i].count_offset;

        elem->store_prop[index] = STORE_PROP;
    }
}

void ply_set_property(PlyProperty* prop, PlyProperty* prop_ptr, PlyElement* elem, const int& index)
{
    prop_ptr->internal_type = prop->internal_type;
    prop_ptr->offset = prop->offset;
    prop_ptr->count_internal = prop->count_internal;
    prop_ptr->count_offset = prop->count_offset;

    elem->store_prop[index] = STORE_PROP;
}

void tokenizeProperties(const char* pnames, std::vector<std::string>& tokens, const std::string& delimiter)
{
    std::string propNames(pnames);
    std::string::size_type start = 0, end;

    while ((end = propNames.find(delimiter, start)) != std::string::npos)
    {
        tokens.push_back(propNames.substr(start, end - start));
        start = end + delimiter.length();
    }

    tokens.push_back(propNames.substr(start));
}

void ply_get_property(PlyFile* plyfile, const char* elem_name, PlyProperty* prop)
{
    PlyElement* elem = find_element(plyfile, elem_name);
    plyfile->which_elem = elem;
    PlyProperty* prop_ptr = nullptr;
    int index;

    if (elem)
    {
        std::vector<std::string> tokens;
        tokenizeProperties(prop->name, tokens, "|");

        for (const auto& token : tokens)
        {
            prop_ptr = find_property(elem, token.c_str(), &index);
            if (prop_ptr) break;
        }
    }

    if (prop_ptr)
    {
        ply_set_property(prop, prop_ptr, elem, index);
    }
    else
    {
        fprintf(stderr, "Warning:  Can't find property '%s' in element '%s'\n", prop->name, elem_name);
    }
}

void ply_get_element(PlyFile* plyfile, void* elem_ptr)
{
    (plyfile->file_type == PLY_ASCII) ? ascii_get_element(plyfile, static_cast<char*>(elem_ptr)) : binary_get_element(plyfile, static_cast<char*>(elem_ptr));
}

char** ply_get_comments(PlyFile* plyfile, int* num_comments)
{
    *num_comments = plyfile->num_comments;
    return plyfile->comments;
}

char** ply_get_obj_info(PlyFile* plyfile, int* num_obj_info)
{
    *num_obj_info = plyfile->num_obj_info;
    return plyfile->obj_info;
}

void setup_other_props(PlyFile*, PlyElement* elem)
{
    int size = 0;

    for (int type_size = 8; type_size > 0; type_size /= 2)
    {
        for (int i = 0; i < elem->nprops; ++i)
        {
            if (elem->store_prop[i]) continue;

            PlyProperty* prop = elem->props[i];
            prop->internal_type = prop->external_type;
            prop->count_internal = prop->count_external;

            if (prop->is_list)
            {
                if (type_size == sizeof(void*))
                {
                    prop->offset = size;
                    size += sizeof(void*);
                }

                if (type_size == ply_type_size[prop->count_external])
                {
                    prop->count_offset = size;
                    size += ply_type_size[prop->count_external];
                }
            }
            else if (type_size == ply_type_size[prop->external_type])
            {
                prop->offset = size;
                size += ply_type_size[prop->external_type];
            }
        }
    }

    elem->other_size = size;
}

PlyOtherProp* ply_get_other_properties(PlyFile* plyfile, char* elem_name, int offset)
{
    PlyElement* elem = find_element(plyfile, elem_name);
    if (elem == nullptr)
    {
        fprintf(stderr, "ply_get_other_properties: Can't find element '%s'\n", elem_name);
        return nullptr;
    }

    plyfile->which_elem = elem;
    elem->other_offset = offset;

    setup_other_props(plyfile, elem);

    PlyOtherProp* other = reinterpret_cast<PlyOtherProp*>(myalloc(sizeof(PlyOtherProp)));
    other->name = strdup(elem_name);
    other->size = elem->other_size;
    other->props = reinterpret_cast<PlyProperty**>(myalloc(sizeof(PlyProperty*) * elem->nprops));

    int nprops = 0;
    for (int i = 0; i < elem->nprops; i++)
    {
        if (elem->store_prop[i] == 0)
        {
            PlyProperty* new_prop = reinterpret_cast<PlyProperty*>(myalloc(sizeof(PlyProperty)));
            copy_property(new_prop, elem->props[i]);
            other->props[nprops++] = new_prop;
        }
    }

    other->nprops = nprops;

    if (nprops == 0)
    {
        elem->other_offset = NO_OTHER_PROPS;
    }

    return other;
}

PlyOtherElems* ply_get_other_element(PlyFile* plyfile, char* elem_name, int elem_count)
{
    PlyElement* elem = find_element(plyfile, elem_name);
    if (elem == nullptr)
    {
        std::string error_msg = "ply_get_other_element: can't find element '" + std::string(elem_name) + "'";
        throw std::runtime_error(error_msg);
    }

    PlyOtherElems* other_elems = plyfile->other_elems;
    if (other_elems == nullptr)
    {
        other_elems = reinterpret_cast<PlyOtherElems*>(myalloc(sizeof(PlyOtherElems)));
        plyfile->other_elems = other_elems;
        other_elems->other_list = reinterpret_cast<OtherElem*>(myalloc(sizeof(OtherElem)));
        other_elems->num_elems = 1;
    }
    else
    {
        other_elems->other_list = reinterpret_cast<OtherElem*>(realloc(other_elems->other_list, sizeof(OtherElem) * (other_elems->num_elems + 1)));
        other_elems->num_elems++;
    }

    OtherElem* other = &other_elems->other_list[other_elems->num_elems - 1];
    other->elem_count = elem_count;
    other->elem_name = strdup(elem_name);
    other->other_data = reinterpret_cast<OtherData**>(malloc(sizeof(OtherData*) * other->elem_count));
    other->other_props = ply_get_other_properties(plyfile, elem_name, offsetof(OtherData, other_props));

    for (int i = 0; i < other->elem_count; i++)
    {
        other->other_data[i] = reinterpret_cast<OtherData*>(malloc(sizeof(OtherData)));
        ply_get_element(plyfile, other->other_data[i]);
    }

    return other_elems;
}



void ply_describe_other_elements(PlyFile* plyfile, PlyOtherElems* other_elems)
{
    if (!other_elems) return;

    plyfile->other_elems = other_elems;

    for (int i = 0; i < other_elems->num_elems; ++i)
    {
        OtherElem* other = &other_elems->other_list[i];
        ply_element_count(plyfile, other->elem_name, other->elem_count);
        ply_describe_other_properties(plyfile, other->other_props, offsetof(OtherData, other_props));
    }
}

void ply_put_other_elements(PlyFile* plyfile)
{
    if (!plyfile->other_elems)
        return;

    for (int i = 0; i < plyfile->other_elems->num_elems; ++i)
    {
        OtherElem* other = &plyfile->other_elems->other_list[i];
        ply_put_element_setup(plyfile, other->elem_name);

        for (int j = 0; j < other->elem_count; ++j)
        {
            ply_put_element(plyfile, static_cast<void*>(other->other_data[j]));
        }
    }
}

void ply_free_other_elements(PlyOtherElems*)
{
    // No implementation needed as per original code
}

void ply_close(PlyFile* plyfile)
{
    fclose(plyfile->fp);

    for (int i = 0; i < plyfile->nelems; ++i)
    {
        PlyElement* elem = plyfile->elems[i];
        free(elem->name);

        for (int j = 0; j < elem->nprops; ++j)
        {
            free(const_cast<char*>(elem->props[j]->name));
            free(elem->props[j]);
        }

        free(elem->props);
        free(elem->store_prop);
        free(elem);
    }
    free(plyfile->elems);

    for (int i = 0; i < plyfile->num_comments; ++i)
    {
        free(plyfile->comments[i]);
    }
    free(plyfile->comments);

    for (int i = 0; i < plyfile->num_obj_info; ++i)
    {
        free(plyfile->obj_info[i]);
    }
    free(plyfile->obj_info);

    free(plyfile);
}

void ply_get_info(PlyFile* ply, float* version, int* file_type)
{
    if (ply)
    {
        *version = ply->version;
        *file_type = ply->file_type;
    }
}

int equal_strings(const char* s1, const char* s2)
{
    while (*s1 && (*s1 == *s2))
    {
        ++s1;
        ++s2;
    }
    return *s1 == *s2;
}

PlyElement* find_element(PlyFile* plyfile, const char* element)
{
    for (int i = 0; i < plyfile->nelems; ++i)
    {
        if (equal_strings(element, plyfile->elems[i]->name))
            return plyfile->elems[i];
    }
    return nullptr;
}

PlyProperty* find_property(PlyElement* elem, const char* prop_name, int* index)
{
    for (int i = 0; i < elem->nprops; ++i)
    {
        if (equal_strings(prop_name, elem->props[i]->name))
        {
            *index = i;
            return elem->props[i];
        }
    }
    *index = -1;
    return nullptr;
}

void ascii_get_element(PlyFile* plyfile, char* elem_ptr)
{
    int j, k;
    PlyElement* elem;
    PlyProperty* prop;
    char** words;
    int nwords; 
    int which_word;
    char* elem_data, * item = 0;
    char* item_ptr = nullptr; 
    int item_size = 0;
    int int_val;
    unsigned int uint_val; 
    double double_val; 
    int list_count;
    int store_it;
    char** store_array = nullptr; 
    char* orig_line = nullptr;  
    char* other_data = nullptr;
    int other_flag;

    elem = plyfile->which_elem;

    if (elem->other_offset != NO_OTHER_PROPS)
    {
        char** ptr;
        other_flag = 1;

        other_data = static_cast<char*>(myalloc(elem->other_size));

        ptr = reinterpret_cast<char**>(elem_ptr + elem->other_offset);
        *ptr = other_data;
    }
    else
    {
        other_flag = 0;
    }

    words = get_words(plyfile->fp, &nwords, &orig_line);
    if (words == nullptr)
    {
        throw std::runtime_error("ply_get_element: unexpected end of file");
    }

    which_word = 0;

    for (j = 0; j < elem->nprops; ++j)
    {
        prop = elem->props[j];
        store_it = (elem->store_prop[j] | other_flag);

        elem_data = store_it ? elem_ptr : other_data;

        if (prop->is_list)
        { 
            get_ascii_item(words[which_word++], prop->count_external, &int_val, &uint_val, &double_val);
            if (store_it)
            {
                item = elem_data + prop->count_offset;
                store_item(item, prop->count_internal, int_val, uint_val, double_val);
            }

            list_count = int_val;
            item_size = ply_type_size[prop->internal_type];
            store_array = reinterpret_cast<char**>(elem_data + prop->offset);

            if (list_count == 0)
            {
                if (store_it)
                {
                    *store_array = nullptr;
                }
            }
            else
            {
                if (store_it)
                {
                    item_ptr = static_cast<char*>(myalloc(sizeof(char) * item_size * list_count));
                    item = item_ptr;
                    *store_array = item_ptr;
                }

                for (k = 0; k < list_count; ++k)
                {
                    get_ascii_item(words[which_word++], prop->external_type, &int_val, &uint_val, &double_val);
                    if (store_it)
                    {
                        store_item(item, prop->internal_type, int_val, uint_val, double_val);
                        item += item_size;
                    }
                }
            }
        }
        else
        {
            get_ascii_item(words[which_word++], prop->external_type, &int_val, &uint_val, &double_val);
            if (store_it)
            {
                item = elem_data + prop->offset;
                store_item(item, prop->internal_type, int_val, uint_val, double_val);
            }
        }
    }

    free(words);
}


void binary_get_element(PlyFile* plyfile, char* elem_ptr)
{
    int j, k;
    PlyElement* elem = plyfile->which_elem;
    PlyProperty* prop;
    char* elem_data;
    char* item_ptr;
    int int_val;
    unsigned int uint_val;
    double double_val;
    int list_count;
    char** store_array;
    char* other_data = nullptr;
    int other_flag = (elem->other_offset != NO_OTHER_PROPS);

    if (other_flag)
    {
        other_data = (char*)myalloc(elem->other_size);
        *(char**)(elem_ptr + elem->other_offset) = other_data;
    }

    for (j = 0; j < elem->nprops; j++)
    {
        prop = elem->props[j];
        int store_it = elem->store_prop[j] | other_flag;
        elem_data = store_it ? elem_ptr : other_data;

        if (prop->is_list)
        {
            get_binary_item(plyfile, prop->count_external, &int_val, &uint_val, &double_val);

            if (store_it)
            {
                store_item(elem_data + prop->count_offset, prop->count_internal, int_val, uint_val, double_val);
            }

            list_count = int_val;

            if (store_it)
            {
                store_array = (char**)(elem_data + prop->offset);
                if (list_count == 0)
                {
                    *store_array = nullptr;
                }
                else
                {
                    item_ptr = (char*)myalloc(ply_type_size[prop->internal_type] * list_count);
                    *store_array = item_ptr;

                    for (k = 0; k < list_count; k++)
                    {
                        get_binary_item(plyfile, prop->external_type, &int_val, &uint_val, &double_val);
                        store_item(item_ptr, prop->internal_type, int_val, uint_val, double_val);
                        item_ptr += ply_type_size[prop->internal_type];
                    }
                }
            }
        }
        else
        {
            get_binary_item(plyfile, prop->external_type, &int_val, &uint_val, &double_val);

            if (store_it)
            {
                store_item(elem_data + prop->offset, prop->internal_type, int_val, uint_val, double_val);
            }
        }
    }
}

void write_scalar_type(FILE* fp, int code)
{
    if (code <= PLY_START_TYPE || code >= PLY_END_TYPE)
    {
        throw std::runtime_error("write_scalar_type: bad data code = " + std::to_string(code));
    }

    fprintf(fp, "%s", type_names[code]);
}

char** get_words(FILE* fp, int* nwords, char** orig_line)
{
#define BIG_STRING 4096
    static char str[BIG_STRING];
    static char str_copy[BIG_STRING];
    char** words = nullptr;
    int max_words = 10;
    int num_words = 0;

    if (!fgets(str, BIG_STRING, fp))
    {
        *nwords = 0;
        *orig_line = nullptr;
        return nullptr;
    }

    words = (char**)myalloc(sizeof(char*) * max_words);

    std::replace(str, str + BIG_STRING - 2, '\t', ' ');
    std::replace(str, str + BIG_STRING - 2, '\n', ' ');
    std::replace(str, str + BIG_STRING - 2, '\r', ' ');

    std::copy(str, str + BIG_STRING, str_copy);

    char* ptr = str;
    while (*ptr != '\0')
    {
        while (*ptr == ' ')
            ptr++;

        if (*ptr == '\0')
            break;

        if (num_words >= max_words)
        {
            max_words += 10;
            words = (char**)realloc(words, sizeof(char*) * max_words);
        }

        words[num_words++] = ptr;

        while (*ptr != ' ')
            ptr++;

        *ptr++ = '\0';
    }

    *nwords = num_words;
    *orig_line = str_copy;
    return words;
}

double get_item_value(char* item, int type)
{
    switch (type)
    {
    case PLY_CHAR:
        return static_cast<double>(*(char*)item);
    case PLY_UCHAR:
    case PLY_UINT8:
        return static_cast<double>(*(unsigned char*)item);
    case PLY_SHORT:
        return static_cast<double>(*(short int*)item);
    case PLY_USHORT:
        return static_cast<double>(*(unsigned short int*)item);
    case PLY_INT:
    case PLY_INT32:
        return static_cast<double>(*(int*)item);
    case PLY_UINT:
        return static_cast<double>(*(unsigned int*)item);
    case PLY_FLOAT:
    case PLY_FLOAT32:
        return static_cast<double>(*(float*)item);
    case PLY_DOUBLE:
        return *(double*)item;
    default:
        fprintf(stderr, "get_item_value: bad type = %d\n", type);
        return 0;
    }
}

void write_binary_item(PlyFile* plyfile, int int_val, unsigned int uint_val, double double_val, int type)
{
    FILE* fp = plyfile->fp;
    unsigned char uchar_val;
    char char_val;
    unsigned short ushort_val;
    short short_val;
    float float_val;

    auto swap_write = [plyfile](void* val, size_t size)
        {
            if (plyfile->file_type == PLY_BINARY_BE)
            {
                if (size == 2) swap2BE(val);
                else if (size == 4) swap4BE(val);
                else if (size == 8) swap8BE(val);
            }
            else
            {
                if (size == 2) swap2LE(val);
                else if (size == 4) swap4LE(val);
                else if (size == 8) swap8LE(val);
            }
        };

    switch (type)
    {
    case PLY_CHAR:
        char_val = int_val;
        fwrite(&char_val, 1, 1, fp);
        break;
    case PLY_SHORT:
        short_val = int_val;
        swap_write(&short_val, 2);
        fwrite(&short_val, 2, 1, fp);
        break;
    case PLY_INT:
    case PLY_INT32:
        swap_write(&int_val, 4);
        fwrite(&int_val, 4, 1, fp);
        break;
    case PLY_UCHAR:
    case PLY_UINT8:
        uchar_val = uint_val;
        fwrite(&uchar_val, 1, 1, fp);
        break;
    case PLY_USHORT:
        ushort_val = uint_val;
        swap_write(&ushort_val, 2);
        fwrite(&ushort_val, 2, 1, fp);
        break;
    case PLY_UINT:
        swap_write(&uint_val, 4);
        fwrite(&uint_val, 4, 1, fp);
        break;
    case PLY_FLOAT:
    case PLY_FLOAT32:
        float_val = double_val;
        swap_write(&float_val, 4);
        fwrite(&float_val, 4, 1, fp);
        break;
    case PLY_DOUBLE:
        swap_write(&double_val, 8);
        fwrite(&double_val, 8, 1, fp);
        break;
    default:
        throw std::runtime_error("write_binary_item: bad type = " + std::to_string(type));
    }
}

void write_ascii_item(FILE* fp, int int_val, unsigned int uint_val, double double_val, int type)
{
    switch (type)
    {
    case PLY_CHAR:
    case PLY_SHORT:
    case PLY_INT:
    case PLY_INT32:
        fprintf(fp, "%d ", int_val);
        break;
    case PLY_UCHAR:
    case PLY_UINT8:
    case PLY_USHORT:
    case PLY_UINT:
        fprintf(fp, "%u ", uint_val);
        break;
    case PLY_FLOAT:
    case PLY_FLOAT32:
    case PLY_DOUBLE:
        fprintf(fp, "%g ", double_val);
        break;
    default:
        throw std::runtime_error("write_ascii_item: bad type = " + std::to_string(type));
    }
}

void get_stored_item(void* ptr, int type, int* int_val, unsigned int* uint_val, double* double_val)
{
    switch (type)
    {
    case PLY_CHAR:
        *int_val = *((char*)ptr);
        *uint_val = *int_val;
        *double_val = *int_val;
        break;
    case PLY_UCHAR:
    case PLY_UINT8:
        *uint_val = *((unsigned char*)ptr);
        *int_val = *uint_val;
        *double_val = *uint_val;
        break;
    case PLY_SHORT:
        *int_val = *((short int*)ptr);
        *uint_val = *int_val;
        *double_val = *int_val;
        break;
    case PLY_USHORT:
        *uint_val = *((unsigned short int*)ptr);
        *int_val = *uint_val;
        *double_val = *uint_val;
        break;
    case PLY_INT:
    case PLY_INT32:
        *int_val = *((int*)ptr);
        *uint_val = *int_val;
        *double_val = *int_val;
        break;
    case PLY_UINT:
        *uint_val = *((unsigned int*)ptr);
        *int_val = *uint_val;
        *double_val = *uint_val;
        break;
    case PLY_FLOAT:
    case PLY_FLOAT32:
        *double_val = *((float*)ptr);
        *int_val = static_cast<int>(*double_val);
        *uint_val = static_cast<unsigned int>(*double_val);
        break;
    case PLY_DOUBLE:
        *double_val = *((double*)ptr);
        *int_val = static_cast<int>(*double_val);
        *uint_val = static_cast<unsigned int>(*double_val);
        break;
    default:
        throw std::runtime_error("get_stored_item: bad type = " + std::to_string(type));
    }
}


void get_binary_item(PlyFile* plyfile, int type, int* int_val, unsigned int* uint_val, double* double_val)
{
    char buffer[8];
    void* ptr = buffer;

    size_t read_size = 0;

    auto handle_read_error = []() {
        throw std::runtime_error("Error in reading PLY file. fread not succeeded.");
        };

    auto swap_bytes = [plyfile, ptr](int size) {
        if (plyfile->file_type == PLY_BINARY_BE) {
            if (size == 2) swap2BE(ptr);
            if (size == 4) swap4BE(ptr);
            if (size == 8) swap8BE(ptr);
        }
        else {
            if (size == 2) swap2LE(ptr);
            if (size == 4) swap4LE(ptr);
            if (size == 8) swap8LE(ptr);
        }
        };

    switch (type)
    {
    case PLY_CHAR:
        read_size = fread(ptr, 1, 1, plyfile->fp);
        if (read_size < 1) handle_read_error();
        *int_val = static_cast<int>(*(char*)ptr);
        *uint_val = static_cast<unsigned int>(*int_val);
        *double_val = static_cast<double>(*int_val);
        break;

    case PLY_UCHAR:
    case PLY_UINT8:
        read_size = fread(ptr, 1, 1, plyfile->fp);
        if (read_size < 1) handle_read_error();
        *uint_val = static_cast<unsigned int>(*(unsigned char*)ptr);
        *int_val = static_cast<int>(*uint_val);
        *double_val = static_cast<double>(*uint_val);
        break;

    case PLY_SHORT:
        read_size = fread(ptr, 2, 1, plyfile->fp);
        if (read_size < 1) handle_read_error();
        swap_bytes(2);
        *int_val = static_cast<int>(*(short int*)ptr);
        *uint_val = static_cast<unsigned int>(*int_val);
        *double_val = static_cast<double>(*int_val);
        break;

    case PLY_USHORT:
        read_size = fread(ptr, 2, 1, plyfile->fp);
        if (read_size < 1) handle_read_error();
        swap_bytes(2);
        *uint_val = static_cast<unsigned int>(*(unsigned short int*)ptr);
        *int_val = static_cast<int>(*uint_val);
        *double_val = static_cast<double>(*uint_val);
        break;

    case PLY_INT:
    case PLY_INT32:
        read_size = fread(ptr, 4, 1, plyfile->fp);
        if (read_size < 1) handle_read_error();
        swap_bytes(4);
        *int_val = *(int*)ptr;
        *uint_val = static_cast<unsigned int>(*int_val);
        *double_val = static_cast<double>(*int_val);
        break;

    case PLY_UINT:
        read_size = fread(ptr, 4, 1, plyfile->fp);
        if (read_size < 1) handle_read_error();
        swap_bytes(4);
        *uint_val = *(unsigned int*)ptr;
        *int_val = static_cast<int>(*uint_val);
        *double_val = static_cast<double>(*uint_val);
        break;

    case PLY_FLOAT:
    case PLY_FLOAT32:
        read_size = fread(ptr, 4, 1, plyfile->fp);
        if (read_size < 1) handle_read_error();
        swap_bytes(4);
        *double_val = static_cast<double>(*(float*)ptr);
        *int_val = static_cast<int>(*double_val);
        *uint_val = static_cast<unsigned int>(*double_val);
        break;

    case PLY_DOUBLE:
        read_size = fread(ptr, 8, 1, plyfile->fp);
        if (read_size < 1) handle_read_error();
        swap_bytes(8);
        *double_val = *(double*)ptr;
        *int_val = static_cast<int>(*double_val);
        *uint_val = static_cast<unsigned int>(*double_val);
        break;

    default:
        throw std::runtime_error("get_binary_item: bad type = " + std::to_string(type));
    }
}

void get_ascii_item(char* word, int type, int* int_val, unsigned int* uint_val, double* double_val)
{
    switch (type)
    {
    case PLY_CHAR:
    case PLY_UCHAR:
    case PLY_UINT8:
    case PLY_SHORT:
    case PLY_USHORT:
    case PLY_INT:
    case PLY_INT32:
        *int_val = std::atoi(word);
        *uint_val = static_cast<unsigned int>(*int_val);
        *double_val = static_cast<double>(*int_val);
        break;

    case PLY_UINT:
        *uint_val = static_cast<unsigned int>(std::strtoul(word, nullptr, 10));
        *int_val = static_cast<int>(*uint_val);
        *double_val = static_cast<double>(*uint_val);
        break;

    case PLY_FLOAT:
    case PLY_FLOAT32:
    case PLY_DOUBLE:
        *double_val = std::stod(word);
        *int_val = static_cast<int>(*double_val);
        *uint_val = static_cast<unsigned int>(*double_val);
        break;

    default:
        throw std::runtime_error("get_ascii_item: bad type = " + std::to_string(type));
    }
}

void store_item(char* item, int type, int int_val, unsigned int uint_val, double double_val)
{
    switch (type)
    {
    case PLY_CHAR:
        *item = static_cast<char>(int_val);
        break;

    case PLY_UCHAR:
    case PLY_UINT8:
        *reinterpret_cast<unsigned char*>(item) = static_cast<unsigned char>(uint_val);
        break;

    case PLY_SHORT:
        *reinterpret_cast<short*>(item) = static_cast<short>(int_val);
        break;

    case PLY_USHORT:
        *reinterpret_cast<unsigned short*>(item) = static_cast<unsigned short>(uint_val);
        break;

    case PLY_INT:
    case PLY_INT32:
        *reinterpret_cast<int*>(item) = int_val;
        break;

    case PLY_UINT:
        *reinterpret_cast<unsigned int*>(item) = uint_val;
        break;

    case PLY_FLOAT:
    case PLY_FLOAT32:
        *reinterpret_cast<float*>(item) = static_cast<float>(double_val);
        break;

    case PLY_DOUBLE:
        *reinterpret_cast<double*>(item) = double_val;
        break;

    default:
        throw std::runtime_error("store_item: bad type = " + std::to_string(type));
    }
}

void add_element(PlyFile* plyfile, char** words, int)
{
    PlyElement* elem;
    elem = (PlyElement*)myalloc(sizeof(PlyElement));
    elem->name = strdup(words[1]);
    elem->num = std::atoi(words[2]);
    elem->nprops = 0;

    plyfile->elems = static_cast<PlyElement**>(plyfile->nelems == 0 ? myalloc(sizeof(PlyElement*)) : realloc(plyfile->elems, sizeof(PlyElement*) * (plyfile->nelems + 1)));

    plyfile->elems[plyfile->nelems++] = elem;
}

int get_prop_type(char* type_name)
{
    for (int i = PLY_START_TYPE + 1; i < PLY_END_TYPE; ++i) {
        if (equal_strings(type_name, type_names[i])) {
            return i;
        }
    }
    return 0;
}

void add_property(PlyFile* plyfile, char** words, int)
{
    PlyProperty* prop;
    PlyElement* elem;
    prop = (PlyProperty*)myalloc(sizeof(PlyProperty));

    if (equal_strings(words[1], "list"))
    {
        prop->count_external = get_prop_type(words[2]);
        prop->external_type = get_prop_type(words[3]);
        prop->name = strdup(words[4]);
        prop->is_list = 1;
    }
    else
    {
        prop->external_type = get_prop_type(words[1]);
        prop->name = strdup(words[2]);
        prop->is_list = 0;
    }

    elem = plyfile->elems[plyfile->nelems - 1];
    if (elem->nprops == 0)
        elem->props = (PlyProperty**)myalloc(sizeof(PlyProperty*));
    else
        elem->props = (PlyProperty**)realloc(elem->props, sizeof(PlyProperty*) * (elem->nprops + 1));

    elem->props[elem->nprops] = prop;
    elem->nprops++;
}
void add_comment(PlyFile* plyfile, char* line)
{
    int i = 7;
    while (line[i] == ' ' || line[i] == '\t') ++i;
    ply_put_comment(plyfile, &line[i]);
}

void add_obj_info(PlyFile* plyfile, char* line)
{
    int i = 8;
    while (line[i] == ' ' || line[i] == '\t') ++i;
    ply_put_obj_info(plyfile, &line[i]);
}

void copy_property(PlyProperty* dest, PlyProperty* src)
{
    dest->name = strdup(src->name);
    dest->external_type = src->external_type;
    dest->internal_type = src->internal_type;
    dest->offset = src->offset;
    dest->is_list = src->is_list;
    dest->count_external = src->count_external;
    dest->count_internal = src->count_internal;
    dest->count_offset = src->count_offset;
}

char* my_alloc(int size, int lnum, const char* fname)
{
    char* ptr = static_cast<char*>(malloc(size));
    if (!ptr) {
        fprintf(stderr, "Memory allocation bombed on line %d in %s\n", lnum, fname);
    }
    return ptr;
}
