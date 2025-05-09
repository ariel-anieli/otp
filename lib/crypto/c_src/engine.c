/*
 * %CopyrightBegin%
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright Ericsson AB 2010-2025. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * %CopyrightEnd%
 */

#include "engine.h"
#include "info.h"

#ifdef HAS_ENGINE_SUPPORT
struct engine_ctx {
    ENGINE *engine;
    int is_functional;
    char *id;
};

#define ERROR_Term(Env, ReasonTerm) enif_make_tuple2((Env), atom_error, (ReasonTerm))
#define ERROR_Atom(Env, ReasonString) ERROR_Term((Env), enif_make_atom((Env),(ReasonString)))

static ErlNifResourceType* engine_ctx_rtype;
static ErlNifMutex *ensure_engine_loaded_mtx = NULL;

static int zero_terminate(ErlNifBinary bin, char **buf);


static void engine_ctx_dtor(ErlNifEnv* env, struct engine_ctx* ctx) {
    if (ctx == NULL)
        return;

    PRINTF_ERR0("engine_ctx_dtor");
    if(ctx->id) {
        PRINTF_ERR1("  non empty ctx->id=%s", ctx->id);
        enif_free(ctx->id);
    } else
         PRINTF_ERR0("  empty ctx->id=NULL");

    if (ctx->engine) {
        if (ctx->is_functional) {
            //printf("\nCalling finish gc\n\n");
            ENGINE_finish(ctx->engine);
        }
        ENGINE_free(ctx->engine);
    }
}

int get_engine_and_key_id(ErlNifEnv *env, ERL_NIF_TERM key, char ** id, ENGINE **e)
{
    ERL_NIF_TERM engine_res, key_id_term;
    struct engine_ctx *ctx;
    ErlNifBinary key_id_bin;

    if (!enif_get_map_value(env, key, atom_engine, &engine_res))
        goto err;
    if (!enif_get_resource(env, engine_res, engine_ctx_rtype, (void**)&ctx))
        goto err;
    if (!enif_get_map_value(env, key, atom_key_id, &key_id_term))
        goto err;
    if (!enif_inspect_binary(env, key_id_term, &key_id_bin))
        goto err;

    *e = ctx->engine;
    return zero_terminate(key_id_bin, id);

 err:
    return 0;
}

char *get_key_password(ErlNifEnv *env, ERL_NIF_TERM key) {
    ERL_NIF_TERM tmp_term;
    ErlNifBinary pwd_bin;
    char *pwd = NULL;

    if (!enif_get_map_value(env, key, atom_password, &tmp_term))
        goto err;
    if (!enif_inspect_binary(env, tmp_term, &pwd_bin))
        goto err;
    if (!zero_terminate(pwd_bin, &pwd))
        goto err;

    return pwd;

 err:
    return NULL;
}

static int zero_terminate(ErlNifBinary bin, char **buf) {
    if ((*buf = enif_alloc(bin.size + 1)) == NULL)
        goto err;

    memcpy(*buf, bin.data, bin.size);
    *(*buf + bin.size) = 0;

    return 1;

 err:
    return 0;
}
#endif /* HAS_ENGINE_SUPPORT */

int init_engine_ctx(ErlNifEnv *env, ErlNifBinary* rt_buf) {
#ifdef HAS_ENGINE_SUPPORT
    engine_ctx_rtype = enif_open_resource_type(env, NULL,
                                               resource_name("ENGINE_CTX", rt_buf),
                                               (ErlNifResourceDtor*) engine_ctx_dtor,
                                               ERL_NIF_RT_CREATE|ERL_NIF_RT_TAKEOVER,
                                               NULL);
    if (engine_ctx_rtype == NULL) {
        PRINTF_ERR0("CRYPTO: Could not open resource type 'ENGINE_CTX'");
        return 0;
    }

#endif

    return 1;
}

int create_engine_mutex(ErlNifEnv *env) {
#ifdef HAS_ENGINE_SUPPORT

    if (!ensure_engine_loaded_mtx && ((ensure_engine_loaded_mtx = enif_mutex_create("crypto.ensure_engine_loaded")) == NULL)) {
        PRINTF_ERR0("CRYPTO: Could not create mutex 'crypto.ensure_engine_loaded'");
        return 0;
    }

#endif
    return 1;
}

void destroy_engine_mutex(ErlNifEnv *env) {
#ifdef HAS_ENGINE_SUPPORT

    enif_mutex_destroy(ensure_engine_loaded_mtx);
    ensure_engine_loaded_mtx = NULL;

#endif
}

#ifdef HAS_ENGINE_SUPPORT
static int get_engine_load_cmd_list(ErlNifEnv* env, const ERL_NIF_TERM term, char **cmds, int i)
{
    ERL_NIF_TERM head, tail;
    const ERL_NIF_TERM *tmp_tuple;
    ErlNifBinary tmpbin;
    int arity;
    char *tuple1 = NULL, *tuple2 = NULL;

    if (enif_is_empty_list(env, term)) {
        cmds[i] = NULL;
        return 0;
    }

    if (!enif_get_list_cell(env, term, &head, &tail))
        goto err;
    if (!enif_get_tuple(env, head, &arity, &tmp_tuple))
        goto err;
    if (arity != 2)
        goto err;
    if (!enif_inspect_binary(env, tmp_tuple[0], &tmpbin))
        goto err;

    if ((tuple1 = enif_alloc(tmpbin.size + 1)) == NULL)
        goto err;

    (void) memcpy(tuple1, tmpbin.data, tmpbin.size);
    tuple1[tmpbin.size] = '\0';
    cmds[i] = tuple1;
    i++;

    if (!enif_inspect_binary(env, tmp_tuple[1], &tmpbin))
        goto err;

    if (tmpbin.size == 0) {
        cmds[i] = NULL;
    } else {
        if ((tuple2 = enif_alloc(tmpbin.size + 1)) == NULL)
            goto err;
        (void) memcpy(tuple2, tmpbin.data, tmpbin.size);
        tuple2[tmpbin.size] = '\0';
        cmds[i] = tuple2;
    }
    i++;
    return get_engine_load_cmd_list(env, tail, cmds, i);

 err:
    if (tuple1 != NULL) {
        i--;
        enif_free(tuple1);
    }
    cmds[i] = NULL;
    return -1;
}

static int register_method(ENGINE *engine, unsigned int method)
{
    int ret = 0;

    switch(method)
    {
#ifdef ENGINE_METHOD_RSA
    case ENGINE_METHOD_RSA:
        ret = ENGINE_register_RSA(engine);
        break;
#endif
#ifdef ENGINE_METHOD_DSA
    case ENGINE_METHOD_DSA:
        ret = ENGINE_register_DSA(engine);
        break;
#endif
#ifdef ENGINE_METHOD_DH
    case ENGINE_METHOD_DH:
        ret = ENGINE_register_DH(engine);
        break;
#endif
#ifdef ENGINE_METHOD_RAND
    case ENGINE_METHOD_RAND:
        ret = ENGINE_register_RAND(engine);
        break;
#endif
#ifdef ENGINE_METHOD_ECDH
    case ENGINE_METHOD_ECDH:
        ret = ENGINE_register_ECDH(engine);
        break;
#endif
#ifdef ENGINE_METHOD_ECDSA
    case ENGINE_METHOD_ECDSA:
        ret = ENGINE_register_ECDSA(engine);
        break;
#endif
#ifdef ENGINE_METHOD_STORE
    case ENGINE_METHOD_STORE:
        ret = ENGINE_register_STORE(engine);
        break;
#endif
#ifdef ENGINE_METHOD_CIPHERS
    case ENGINE_METHOD_CIPHERS:
        ret = ENGINE_register_ciphers(engine);
        break;
#endif
#ifdef ENGINE_METHOD_DIGESTS
    case ENGINE_METHOD_DIGESTS:
        ret = ENGINE_register_digests(engine);
        break;
#endif
#ifdef ENGINE_METHOD_PKEY_METHS
    case ENGINE_METHOD_PKEY_METHS:
        ret = ENGINE_register_pkey_meths(engine);
        break;
#endif
#ifdef ENGINE_METHOD_PKEY_ASN1_METHS
    case ENGINE_METHOD_PKEY_ASN1_METHS:
        ret = ENGINE_register_pkey_asn1_meths(engine);
        break;
#endif
#ifdef ENGINE_METHOD_EC
    case ENGINE_METHOD_EC:
        ret = ENGINE_register_EC(engine);
        break;
#endif
    default:
        return -1;
    }

    return ret;
}

static void unregister_method(ENGINE *engine, unsigned int method)
{

    switch(method)
    {
#ifdef ENGINE_METHOD_RSA
    case ENGINE_METHOD_RSA:
        ENGINE_unregister_RSA(engine);
        break;
#endif
#ifdef ENGINE_METHOD_DSA
    case ENGINE_METHOD_DSA:
        ENGINE_unregister_DSA(engine);
        break;
#endif
#ifdef ENGINE_METHOD_DH
    case ENGINE_METHOD_DH:
        ENGINE_unregister_DH(engine);
        break;
#endif
#ifdef ENGINE_METHOD_RAND
    case ENGINE_METHOD_RAND:
        ENGINE_unregister_RAND(engine);
        break;
#endif
#ifdef ENGINE_METHOD_ECDH
    case ENGINE_METHOD_ECDH:
        ENGINE_unregister_ECDH(engine);
        break;
#endif
#ifdef ENGINE_METHOD_ECDSA
    case ENGINE_METHOD_ECDSA:
        ENGINE_unregister_ECDSA(engine);
        break;
#endif
#ifdef ENGINE_METHOD_STORE
    case ENGINE_METHOD_STORE:
        ENGINE_unregister_STORE(engine);
        break;
#endif
#ifdef ENGINE_METHOD_CIPHERS
    case ENGINE_METHOD_CIPHERS:
        ENGINE_unregister_ciphers(engine);
        break;
#endif
#ifdef ENGINE_METHOD_DIGESTS
    case ENGINE_METHOD_DIGESTS:
        ENGINE_unregister_digests(engine);
        break;
#endif
#ifdef ENGINE_METHOD_PKEY_METHS
    case ENGINE_METHOD_PKEY_METHS:
        ENGINE_unregister_pkey_meths(engine);
        break;
#endif
#ifdef ENGINE_METHOD_PKEY_ASN1_METHS
    case ENGINE_METHOD_PKEY_ASN1_METHS:
        ENGINE_unregister_pkey_asn1_meths(engine);
        break;
#endif
#ifdef ENGINE_METHOD_EC
    case ENGINE_METHOD_EC:
        ENGINE_unregister_EC(engine);
        break;
#endif
    default:
        break;
    }

    return;
}

#endif /* HAS_ENGINE_SUPPORT */

ERL_NIF_TERM engine_by_id_nif(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{/* (EngineId) */
#ifdef HAS_ENGINE_SUPPORT
    ERL_NIF_TERM ret, result;
    ErlNifBinary engine_id_bin;
    char *engine_id = NULL;
    ENGINE *engine;
    struct engine_ctx *ctx = NULL;

    /* Get Arguments */
    ASSERT(argc == 1);

    /* EngineId */
    if (!enif_inspect_binary(env, argv[0], &engine_id_bin))
        goto bad_arg;

    if ((engine_id = enif_alloc(engine_id_bin.size+1)) == NULL)
        goto err;
    (void) memcpy(engine_id, engine_id_bin.data, engine_id_bin.size);
    engine_id[engine_id_bin.size] = '\0';


    if ((engine = ENGINE_by_id(engine_id)) == NULL) {
        PRINTF_ERR0("engine_by_id_nif Leaved: {error, bad_engine_id}");
        ret = ERROR_Atom(env, "bad_engine_id");
        goto done;
    }
    if ((ctx = enif_alloc_resource(engine_ctx_rtype, sizeof(struct engine_ctx))) == NULL)
        goto err;
    ctx->engine = engine;
    ctx->is_functional = 0;
    ctx->id = engine_id;
    /* ctx now owns engine_id */
    engine_id = NULL;

    result = enif_make_resource(env, ctx);
    ret = enif_make_tuple2(env, atom_ok, result);
    goto done;

 bad_arg:
 err:
    ret = enif_make_badarg(env);

 done:
    if (engine_id)
        enif_free(engine_id);
    if (ctx)
        enif_release_resource(ctx);
    return ret;

#else
    return RAISE_NOTSUP(env);
#endif
}

ERL_NIF_TERM engine_init_nif(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{/* (Engine) */
#ifdef HAS_ENGINE_SUPPORT
    struct engine_ctx *ctx;

    /* Get Arguments */
    ASSERT(argc == 1);

    /* Engine */
    if (!enif_get_resource(env, argv[0], engine_ctx_rtype, (void**)&ctx))
        goto bad_arg;

    if (!ENGINE_init(ctx->engine))
        return ERROR_Atom(env, "engine_init_failed");
    ctx->is_functional = 1;
    return atom_ok;

 bad_arg:
    return enif_make_badarg(env);

#else
    return RAISE_NOTSUP(env);
#endif
}

ERL_NIF_TERM engine_free_nif(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{/* (Engine) */
#ifdef HAS_ENGINE_SUPPORT
    struct engine_ctx *ctx;

    // Get Engine
    ASSERT(argc == 1);

    if (!enif_get_resource(env, argv[0], engine_ctx_rtype, (void**)&ctx))
        goto bad_arg;

    if (ctx->engine) {
        if (ctx->is_functional) {
            printf("\nCalling finish\n\n");
            if (!ENGINE_finish(ctx->engine))
                goto err;
            ctx->is_functional = 0;
        }
        if (!ENGINE_free(ctx->engine))
            goto err;
        ctx->engine = NULL;
    }
    else {
        ASSERT(!ctx->is_functional);
    }
    
    return atom_ok;

 bad_arg:
 err:
    return enif_make_badarg(env);
#else
    return RAISE_NOTSUP(env);
#endif
}

ERL_NIF_TERM engine_load_dynamic_nif(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{/* () */
#ifdef HAS_ENGINE_SUPPORT
    ASSERT(argc == 0);

# if OPENSSL_VERSION_NUMBER < PACKED_OPENSSL_VERSION_PLAIN(1,1,0)
    ENGINE_load_dynamic();
# else
    OPENSSL_init_crypto(OPENSSL_INIT_ENGINE_DYNAMIC, NULL);
# endif
    return atom_ok;
#else
    return RAISE_NOTSUP(env);
#endif
}

ERL_NIF_TERM engine_ctrl_cmd_strings_nif(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{/* (Engine, Commands, Optional) */
#ifdef HAS_ENGINE_SUPPORT
    ERL_NIF_TERM ret;
    unsigned int cmds_len = 0;
    char **cmds = NULL;
    struct engine_ctx *ctx;
    unsigned int i;
    int optional = 0;
    int cmds_loaded = 0;

    /* Get Arguments */
    ASSERT(argc == 3);

    /* Engine */
    if (!enif_get_resource(env, argv[0], engine_ctx_rtype, (void**)&ctx)
        || !ctx->engine)
        goto bad_arg;

    PRINTF_ERR1("Engine Id:  %s\r\n", ENGINE_get_id(ctx->engine));
    /* Command List */
    if (!enif_get_list_length(env, argv[1], &cmds_len))
        goto bad_arg;

    if (cmds_len > (UINT_MAX / 2) - 1)
        goto err;
    cmds_len *= 2; // Key-Value list from erlang

    if ((size_t)cmds_len + 1 > SIZE_MAX / sizeof(char*))
        goto err;
    if ((cmds = enif_alloc((cmds_len + 1) * sizeof(char*))) == NULL)
        goto err;
    if (get_engine_load_cmd_list(env, argv[1], cmds, 0))
        goto err;
    cmds_loaded = 1;
    if (!enif_get_int(env, argv[2], &optional))
        goto err;

    for(i = 0; i < cmds_len; i+=2) {
        PRINTF_ERR2("Cmd:  %s:%s\r\n",
                   cmds[i] ? cmds[i] : "(NULL)",
                   cmds[i+1] ? cmds[i+1] : "(NULL)");
        if(!ENGINE_ctrl_cmd_string(ctx->engine, cmds[i], cmds[i+1], optional)) {
            PRINTF_ERR2("Command failed:  %s:%s\r\n",
                        cmds[i] ? cmds[i] : "(NULL)",
                        cmds[i+1] ? cmds[i+1] : "(NULL)");
            goto cmd_failed;
        }
    }
    ret = atom_ok;
    goto done;

 bad_arg:
 err:
    ret = enif_make_badarg(env);
    goto done;

 cmd_failed:
    ret = ERROR_Atom(env, "ctrl_cmd_failed");

 done:
    if (cmds_loaded) {
        for (i = 0; cmds != NULL && cmds[i] != NULL; i++)
            enif_free(cmds[i]);
    }

    if (cmds != NULL)
        enif_free(cmds);

    return ret;

#else
    return RAISE_NOTSUP(env);
#endif
}

ERL_NIF_TERM engine_add_nif(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{/* (Engine) */
#ifdef HAS_ENGINE_SUPPORT
    struct engine_ctx *ctx;

    /* Get Arguments */
    ASSERT(argc == 1);

    /* Engine */
    if (!enif_get_resource(env, argv[0], engine_ctx_rtype, (void**)&ctx)
        || !ctx->engine)
        goto bad_arg;

    if (!ENGINE_add(ctx->engine))
        goto failed;

    return atom_ok;

 bad_arg:
    return enif_make_badarg(env);

 failed:
    return ERROR_Atom(env, "add_engine_failed");

#else
    return RAISE_NOTSUP(env);
#endif
}

ERL_NIF_TERM engine_remove_nif(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{/* (Engine) */
#ifdef HAS_ENGINE_SUPPORT
    struct engine_ctx *ctx;

    /* Get Arguments */
    ASSERT(argc == 1);

    /* Engine */
    if (!enif_get_resource(env, argv[0], engine_ctx_rtype, (void**)&ctx)
        || !ctx->engine)
        goto bad_arg;

    if (!ENGINE_remove(ctx->engine))
        goto failed;

    return atom_ok;

 bad_arg:
    return enif_make_badarg(env);

 failed:
    return ERROR_Atom(env, "remove_engine_failed");
#else
    return RAISE_NOTSUP(env);
#endif
}

ERL_NIF_TERM engine_register_nif(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{/* (Engine, EngineMethod) */
#ifdef HAS_ENGINE_SUPPORT
    struct engine_ctx *ctx;
    unsigned int method;

    /* Get Arguments */
    ASSERT(argc == 2);

    /* Engine */
    if (!enif_get_resource(env, argv[0], engine_ctx_rtype, (void**)&ctx)
        || !ctx->engine)
        goto bad_arg;
    if (!enif_get_uint(env, argv[1], &method))
        goto bad_arg;

    switch(register_method(ctx->engine, method))
    {
    case 1:
        break;
    case 0:
        goto failed;
        break;
    case -1:
        goto not_supported;
        break;
    }

    return atom_ok;

 bad_arg:
    return enif_make_badarg(env);

 failed:
    return ERROR_Atom(env, "register_engine_failed");

 not_supported:
     return ERROR_Atom(env, "engine_method_not_supported");

#else
    return RAISE_NOTSUP(env);
#endif
}

ERL_NIF_TERM engine_unregister_nif(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{/* (Engine, EngineMethod) */
#ifdef HAS_ENGINE_SUPPORT
    struct engine_ctx *ctx;
    unsigned int method;

    /* Get Arguments */
    ASSERT(argc == 2);

    /* Engine */
    if (!enif_get_resource(env, argv[0], engine_ctx_rtype, (void**)&ctx)
        || !ctx->engine)
        goto bad_arg;
    if (!enif_get_uint(env, argv[1], &method))
        goto bad_arg;

    unregister_method(ctx->engine, method);

    return atom_ok;

 bad_arg:
    return enif_make_badarg(env);

#else
    return RAISE_NOTSUP(env);
#endif
}

ERL_NIF_TERM engine_get_first_nif(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{/* () */
#ifdef HAS_ENGINE_SUPPORT
    ERL_NIF_TERM ret, result;
    ENGINE *engine;
    ErlNifBinary engine_bin;
    struct engine_ctx *ctx = NULL;

    ASSERT(argc == 0);

    if ((engine = ENGINE_get_first()) == NULL) {
        if (!enif_alloc_binary(0, &engine_bin))
            goto err;
        engine_bin.size = 0;
        return enif_make_tuple2(env, atom_ok, enif_make_binary(env, &engine_bin));
    }

    if ((ctx = enif_alloc_resource(engine_ctx_rtype, sizeof(struct engine_ctx))) == NULL)
        goto err;
    ctx->is_functional = 0;
    ctx->engine = engine;
    ctx->id = NULL;

    result = enif_make_resource(env, ctx);
    ret = enif_make_tuple2(env, atom_ok, result);
    goto done;

 err:
    ret = enif_make_badarg(env);

 done:
    if (ctx)
        enif_release_resource(ctx);
    return ret;

#else
    return RAISE_NOTSUP(env);
#endif
}

ERL_NIF_TERM engine_get_next_nif(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{/* (Engine) */
#ifdef HAS_ENGINE_SUPPORT
    ERL_NIF_TERM ret, result;
    ENGINE *engine;
    ErlNifBinary engine_bin;
    struct engine_ctx *ctx, *next_ctx = NULL;

    /* Get Arguments */
    ASSERT(argc == 1);

    /* Engine */
    if (!enif_get_resource(env, argv[0], engine_ctx_rtype, (void**)&ctx)
        || !ctx->engine)
        goto bad_arg;

    if (ctx->is_functional) {
        ENGINE_finish(ctx->engine);
        ctx->is_functional = 0;
    }
    engine = ENGINE_get_next(ctx->engine);
    ctx->engine = NULL;

    if (engine == NULL) {
        if (!enif_alloc_binary(0, &engine_bin))
            goto err;
        engine_bin.size = 0;
        return enif_make_tuple2(env, atom_ok, enif_make_binary(env, &engine_bin));
    }

    if ((next_ctx = enif_alloc_resource(engine_ctx_rtype, sizeof(struct engine_ctx))) == NULL)
        goto err;
    next_ctx->engine = engine;
    next_ctx->is_functional = 0;
    next_ctx->id = NULL;

    result = enif_make_resource(env, next_ctx);
    ret = enif_make_tuple2(env, atom_ok, result);
    goto done;

 bad_arg:
 err:
    ret = enif_make_badarg(env);

 done:
    if (next_ctx)
        enif_release_resource(next_ctx);
    return ret;

#else
    return RAISE_NOTSUP(env);
#endif
}

ERL_NIF_TERM engine_get_id_nif(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{/* (Engine) */
#ifdef HAS_ENGINE_SUPPORT
    ErlNifBinary engine_id_bin;
    const char *engine_id;
    size_t size;
    struct engine_ctx *ctx = NULL;

    // Get arguments
    ASSERT(argc == 1);

    if (!enif_get_resource(env, argv[0], engine_ctx_rtype, (void**)&ctx)
        || !ctx->engine)
        goto bad_arg;

    if ((engine_id = ENGINE_get_id(ctx->engine)) == NULL) {
        if (!enif_alloc_binary(0, &engine_id_bin))
            goto err;
        engine_id_bin.size = 0;
        return enif_make_binary(env, &engine_id_bin);
    }

    size = strlen(engine_id);
    if (!enif_alloc_binary(size, &engine_id_bin))
        goto err;
    engine_id_bin.size = size;
    memcpy(engine_id_bin.data, engine_id, size);

    return enif_make_binary(env, &engine_id_bin);

 bad_arg:
 err:
    return enif_make_badarg(env);

#else
    return RAISE_NOTSUP(env);
#endif
}

ERL_NIF_TERM engine_get_name_nif(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{/* (Engine) */
#ifdef HAS_ENGINE_SUPPORT
    ErlNifBinary engine_name_bin;
    const char *engine_name;
    size_t size;
    struct engine_ctx *ctx;

    // Get Engine
    ASSERT(argc == 1);

    if (!enif_get_resource(env, argv[0], engine_ctx_rtype, (void**)&ctx)
        || !ctx->engine)
        goto bad_arg;

    if ((engine_name = ENGINE_get_name(ctx->engine)) == NULL) {
        if (!enif_alloc_binary(0, &engine_name_bin))
            goto err;
        engine_name_bin.size = 0;
        return enif_make_binary(env, &engine_name_bin);
    }

    size = strlen(engine_name);
    if (!enif_alloc_binary(size, &engine_name_bin))
        goto err;
    engine_name_bin.size = size;
    memcpy(engine_name_bin.data, engine_name, size);

    return enif_make_binary(env, &engine_name_bin);

 bad_arg:
 err:
    return enif_make_badarg(env);

#else
    return RAISE_NOTSUP(env);
#endif
}

ERL_NIF_TERM engine_get_all_methods_nif(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{/* () */
#ifdef HAS_ENGINE_SUPPORT
    ERL_NIF_TERM method_array[12];
    unsigned int i = 0;

    ASSERT(argc == 0);

#ifdef ENGINE_METHOD_RSA
    method_array[i++] = atom_engine_method_rsa;
#endif
#ifdef ENGINE_METHOD_DSA
    method_array[i++] = atom_engine_method_dsa;
#endif
#ifdef ENGINE_METHOD_DH
    method_array[i++] = atom_engine_method_dh;
#endif
#ifdef ENGINE_METHOD_RAND
    method_array[i++] = atom_engine_method_rand;
#endif
#ifdef ENGINE_METHOD_ECDH
    method_array[i++] = atom_engine_method_ecdh;
#endif
#ifdef ENGINE_METHOD_ECDSA
    method_array[i++] = atom_engine_method_ecdsa;
#endif
#ifdef ENGINE_METHOD_STORE
    method_array[i++] = atom_engine_method_store;
#endif
#ifdef ENGINE_METHOD_CIPHERS
    method_array[i++] = atom_engine_method_ciphers;
#endif
#ifdef ENGINE_METHOD_DIGESTS
    method_array[i++] = atom_engine_method_digests;
#endif
#ifdef ENGINE_METHOD_PKEY_METHS
    method_array[i++] = atom_engine_method_pkey_meths;
#endif
#ifdef ENGINE_METHOD_PKEY_ASN1_METHS
    method_array[i++] = atom_engine_method_pkey_asn1_meths;
#endif
#ifdef ENGINE_METHOD_EC
    method_array[i++] = atom_engine_method_ec;
#endif

    return enif_make_list_from_array(env, method_array, i);
#else
    return RAISE_NOTSUP(env);
#endif
}

ERL_NIF_TERM ensure_engine_loaded_nif(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{/* (EngineId, LibPath) */
#ifdef HAS_ENGINE_SUPPORT
    ERL_NIF_TERM ret, result;
    ErlNifBinary engine_id_bin,
        library_path_bin;
    char *engine_id = NULL,
        *library_path = NULL;
    int is_locked = 0;

    ENGINE *engine = NULL;
    struct engine_ctx *ctx = NULL;

    /* Get Arguments */
    ASSERT(argc == 2);

    /* EngineId */
    if (!enif_inspect_binary(env, argv[0], &engine_id_bin))
        goto bad_arg;

    if ((engine_id = enif_alloc(engine_id_bin.size+1)) == NULL)
        goto bad_arg;

    (void) memcpy(engine_id, engine_id_bin.data, engine_id_bin.size);
    engine_id[engine_id_bin.size] = '\0';

    /* LibPath */
    if (!enif_inspect_binary(env, argv[1], &library_path_bin))
        goto bad_arg;

    if ((library_path = enif_alloc(library_path_bin.size+1)) == NULL)
        goto bad_arg;

    (void) memcpy(library_path, library_path_bin.data, library_path_bin.size);
    library_path[library_path_bin.size] = '\0';

    /* Loading Engine */
    enif_mutex_lock(ensure_engine_loaded_mtx);
    is_locked = 1;

    if ((engine = ENGINE_by_id(engine_id)) == NULL)
    {
        PRINTF_ERR0("Load engine\r\n");
        /* Load dynamic engine */
        ENGINE_load_dynamic();
        if ((engine = ENGINE_by_id("dynamic")) == NULL) {
            PRINTF_ERR0("ensure_engine_loaded_nif; couldn't get the dynamic engine");
            ret = ERROR_Atom(env, "bad_engine_id");
            goto done;
        }

        /* Use dynamic engine to load the real engine */
        /* From this point an ENGINE_free() is done on failure */
        if(!ENGINE_ctrl_cmd_string(engine, "SO_PATH", library_path, 0)) {
            PRINTF_ERR1("Cmd: SO_PATH:%s\r\n", library_path);
            ret = ERROR_Atom(env, "ctrl_cmd_failed");
            goto err;
        }
        if(!ENGINE_ctrl_cmd_string(engine, "ID", engine_id, 0)) {
            PRINTF_ERR1("Cmd: ID:%s\r\n", engine_id);
            ret = ERROR_Atom(env, "ctrl_cmd_failed");
            goto err;
        }
        if(!ENGINE_ctrl_cmd_string(engine, "LOAD", NULL, 0)) {
            PRINTF_ERR0("Cmd:  LOAD:(NULL)\r\n");
            ret = ERROR_Atom(env, "ctrl_cmd_failed");
            goto err;
        }

        /* Add engine to OpenSSls internal list */
        if(!ENGINE_add(engine)) {
            ret = ERROR_Atom(env, "add_engine_failed");
            goto err;
        }
    }
  
    PRINTF_ERR0("Initialize engine\r\n");

    /* Init engine and get functional reference */
    if (!ENGINE_init(engine)) {
        ret = ERROR_Atom(env, "engine_init_failed");
        goto err;
    }
        
    /* Get structural reference to already loaded engine */
    if ((ctx = enif_alloc_resource(engine_ctx_rtype, sizeof(struct engine_ctx))) == NULL) {
        ret = enif_make_badarg(env);
        ENGINE_finish(engine);
        goto err;
    }
    ctx->engine = engine;
    ctx->is_functional = 1;
    ctx->id = engine_id;
    /* ctx now owns engine_id */
    engine_id = NULL;

    result = enif_make_resource(env, ctx);
    ret = enif_make_tuple2(env, atom_ok, result);
    goto done;

 bad_arg:
    ret = enif_make_badarg(env);

 err:
    if(engine)
        ENGINE_free(engine);

 done:

    enif_free(library_path);

    if(is_locked) {
        enif_mutex_unlock(ensure_engine_loaded_mtx);
        is_locked = 0;
    }

    if (engine_id)
        enif_free(engine_id);
    if (ctx)
        enif_release_resource(ctx);

    return ret;

#else
    return RAISE_NOTSUP(env);
#endif
}
