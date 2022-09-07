#ifndef JX_PARSER_H
#define JX_PARSER_H

#include <stddef.h>

struct jx_parser
{
    int bits;
    int nnodes;
    unsigned pos;
    unsigned toknext;
    int toksuper;
};

struct jx_node;

void parser_init(struct jx_parser *parser, int bits);
int parser_parse(struct jx_parser *, size_t length, char *json, int nnodes,
                 struct jx_node *);

#endif
