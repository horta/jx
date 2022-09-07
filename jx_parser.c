#include "jx_parser.h"
#include "jx_error.h"
#include "jx_node.h"
#include "jx_type.h"
#include <assert.h>
#include <stdbool.h>

static int parse_primitive(struct jx_parser *parser, const char *js,
                           const size_t len, struct jx_node *tokens,
                           const size_t num_tokens);
static int parse_string(struct jx_parser *parser, const char *js,
                        const size_t len, struct jx_node *tokens,
                        const size_t num_tokens);
static int primitive_type(char c);
static void fill_node(struct jx_node *token, const int type, const int start,
                      const int end);

void parser_init(struct jx_parser *parser, int bits)
{
    parser->bits = bits;
    parser->nnodes = 0;
    parser->pos = 0;
    parser->toknext = 0;
    parser->toksuper = -1;
}

int parser_parse(struct jx_parser *parser, const size_t len, char *js,
                 int nnodes, struct jx_node *nodes)
{
    int r;
    int i;
    struct jx_node *token;
    int count = parser->toknext;

    for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++)
    {
        char c;
        int type;

        c = js[parser->pos];
        switch (c)
        {
        case '{':
        case '[':
            count++;
            if (nodes == NULL)
            {
                break;
            }
            token = node_alloc(parser, nnodes, nodes);
            if (token == NULL)
            {
                return JX_NOMEM;
            }
            if (parser->toksuper != -1)
            {
                struct jx_node *t = &nodes[parser->toksuper];
                /* In strict mode an object or array can't become a key */
                if (t->type == JX_OBJECT)
                {
                    return JX_INVAL;
                }
                t->size++;
                token->parent = parser->toksuper;
            }
            token->type = (c == '{' ? JX_OBJECT : JX_ARRAY);
            token->start = parser->pos;
            parser->toksuper = parser->toknext - 1;
            break;
        case '}':
        case ']':
            if (nodes == NULL)
            {
                break;
            }
            type = (c == '}' ? JX_OBJECT : JX_ARRAY);
            if (parser->toknext < 1)
            {
                return JX_INVAL;
            }
            token = &nodes[parser->toknext - 1];
            for (;;)
            {
                if (token->start != -1 && token->end == -1)
                {
                    if (token->type != type)
                    {
                        return JX_INVAL;
                    }
                    token->end = parser->pos + 1;
                    parser->toksuper = token->parent;
                    break;
                }
                if (token->parent == -1)
                {
                    if (token->type != type || parser->toksuper == -1)
                    {
                        return JX_INVAL;
                    }
                    break;
                }
                token = &nodes[token->parent];
            }
            break;
        case '\"':
            r = parse_string(parser, js, len, nodes, nnodes);
            if (r < 0)
            {
                return r;
            }
            count++;
            if (parser->toksuper != -1 && nodes != NULL)
            {
                nodes[parser->toksuper].size++;
            }
            break;
        case '\t':
        case '\r':
        case '\n':
        case ' ':
            break;
        case ':':
            parser->toksuper = parser->toknext - 1;
            break;
        case ',':
            if (nodes != NULL && parser->toksuper != -1 &&
                nodes[parser->toksuper].type != JX_ARRAY &&
                nodes[parser->toksuper].type != JX_OBJECT)
            {
                parser->toksuper = nodes[parser->toksuper].parent;
            }
            break;
        /* In strict mode primitives are: numbers and booleans */
        case '-':
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
        case 't':
        case 'f':
        case 'n':
            /* And they must not be keys of the object */
            if (nodes != NULL && parser->toksuper != -1)
            {
                const struct jx_node *t = &nodes[parser->toksuper];
                if (t->type == JX_OBJECT ||
                    (t->type == JX_STRING && t->size != 0))
                {
                    return JX_INVAL;
                }
            }
            r = parse_primitive(parser, js, len, nodes, nnodes);
            if (r < 0)
            {
                return r;
            }
            count++;
            if (parser->toksuper != -1 && nodes != NULL)
            {
                nodes[parser->toksuper].size++;
            }
            break;

        /* Unexpected char in strict mode */
        default:
            return JX_INVAL;
        }
    }

    if (nodes != NULL)
    {
        for (i = parser->toknext - 1; i >= 0; i--)
        {
            /* Unmatched opened object or array */
            if (nodes[i].start != -1 && nodes[i].end == -1)
            {
                return JX_INVAL;
            }
        }
    }

    return count;
}

static int parse_primitive(struct jx_parser *parser, const char *js,
                           const size_t len, struct jx_node *tokens,
                           const size_t num_tokens)
{
    struct jx_node *token;
    int start;

    start = parser->pos;

    for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++)
    {
        switch (js[parser->pos])
        {
        case '\t':
        case '\r':
        case '\n':
        case ' ':
        case ',':
        case ']':
        case '}':
            goto found;
        default:
            /* to quiet a warning from gcc*/
            break;
        }
        if (js[parser->pos] < 32 || js[parser->pos] >= 127)
        {
            parser->pos = start;
            return JX_INVAL;
        }
    }
    /* In strict mode primitive must be followed by a comma/object/array */
    parser->pos = start;
    return JX_INVAL;

found:
    if (tokens == NULL)
    {
        parser->pos--;
        return 0;
    }
    token = node_alloc(parser, num_tokens, tokens);
    if (token == NULL)
    {
        parser->pos = start;
        return JX_NOMEM;
    }
    fill_node(token, primitive_type(js[start]), start, parser->pos);
    token->parent = parser->toksuper;
    parser->pos--;
    return 0;
}

static int parse_string(struct jx_parser *parser, const char *js,
                        const size_t len, struct jx_node *tokens,
                        const size_t num_tokens)
{
    struct jx_node *token;

    int start = parser->pos;

    /* Skip starting quote */
    parser->pos++;

    for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++)
    {
        char c = js[parser->pos];

        /* Quote: end of string */
        if (c == '\"')
        {
            if (tokens == NULL)
            {
                return 0;
            }
            token = node_alloc(parser, num_tokens, tokens);
            if (token == NULL)
            {
                parser->pos = start;
                return JX_NOMEM;
            }
            fill_node(token, JX_STRING, start + 1, parser->pos);
            token->parent = parser->toksuper;
            return 0;
        }

        /* Backslash: Quoted symbol expected */
        if (c == '\\' && parser->pos + 1 < len)
        {
            int i;
            parser->pos++;
            switch (js[parser->pos])
            {
            /* Allowed escaped symbols */
            case '\"':
            case '/':
            case '\\':
            case 'b':
            case 'f':
            case 'r':
            case 'n':
            case 't':
                break;
            /* Allows escaped symbol \uXXXX */
            case 'u':
                parser->pos++;
                for (i = 0;
                     i < 4 && parser->pos < len && js[parser->pos] != '\0'; i++)
                {
                    /* If it isn't a hex character we have an error */
                    if (!((js[parser->pos] >= 48 &&
                           js[parser->pos] <= 57) || /* 0-9 */
                          (js[parser->pos] >= 65 &&
                           js[parser->pos] <= 70) || /* A-F */
                          (js[parser->pos] >= 97 && js[parser->pos] <= 102)))
                    { /* a-f */
                        parser->pos = start;
                        return JX_INVAL;
                    }
                    parser->pos++;
                }
                parser->pos--;
                break;
            /* Unexpected symbol */
            default:
                parser->pos = start;
                return JX_INVAL;
            }
        }
    }
    parser->pos = start;
    return JX_INVAL;
}

static int primitive_type(char c)
{
    switch (c)
    {
    case '-':
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        return JX_NUMBER;
    case 't':
    case 'f':
        return JX_BOOL;
        break;
    case 'n':
        return JX_NULL;
        break;
    default:
        assert(false);
    }
    assert(false);
}

static void fill_node(struct jx_node *token, const int type, const int start,
                      const int end)
{
    token->type = type;
    token->start = start;
    token->end = end;
    token->size = 0;
}
