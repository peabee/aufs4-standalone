/*
 * Copyright (C) 2005-2015 Junjiro R. Okajima
 *
 * This program, aufs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * mount options/flags
 */

#include <linux/namei.h>
#include <linux/types.h> /* a distribution requires */
#include <linux/parser.h>
#include "aufs.h"

/* ---------------------------------------------------------------------- */

enum {
	Opt_br,
	Opt_add,
	Opt_rdcache, Opt_rdblk, Opt_rdhash,
	Opt_rdblk_def, Opt_rdhash_def,
	Opt_xino, Opt_noxino,
	Opt_plink, Opt_noplink, Opt_list_plink,
	Opt_wbr_copyup, Opt_wbr_create,
	Opt_tail, Opt_ignore, Opt_ignore_silent, Opt_err
};

static match_table_t options = {
	{Opt_br, "br=%s"},
	{Opt_br, "br:%s"},

	{Opt_xino, "xino=%s"},
	{Opt_noxino, "noxino"},

#ifdef CONFIG_PROC_FS
	{Opt_plink, "plink"},
#else
	{Opt_ignore_silent, "plink"},
#endif

	{Opt_noplink, "noplink"},

#ifdef CONFIG_AUFS_DEBUG
	{Opt_list_plink, "list_plink"},
#endif

	{Opt_rdcache, "rdcache=%d"},
	{Opt_rdblk, "rdblk=%d"},
	{Opt_rdblk_def, "rdblk=def"},
	{Opt_rdhash, "rdhash=%d"},
	{Opt_rdhash_def, "rdhash=def"},

	{Opt_wbr_create, "create=%s"},
	{Opt_wbr_create, "create_policy=%s"},
	{Opt_wbr_copyup, "cpup=%s"},
	{Opt_wbr_copyup, "copyup=%s"},
	{Opt_wbr_copyup, "copyup_policy=%s"},

	/* internal use for the scripts */
	{Opt_ignore_silent, "si=%s"},

	/* temporary workaround, due to old mount(8)? */
	{Opt_ignore_silent, "relatime"},

	{Opt_err, NULL}
};

/* ---------------------------------------------------------------------- */

static const char *au_parser_pattern(int val, match_table_t tbl)
{
	struct match_token *p;

	p = tbl;
	while (p->pattern) {
		if (p->token == val)
			return p->pattern;
		p++;
	}
	BUG();
	return "??";
}

static const char *au_optstr(int *val, match_table_t tbl)
{
	struct match_token *p;
	int v;

	v = *val;
	if (!v)
		goto out;
	p = tbl;
	while (p->pattern) {
		if (p->token
		    && (v & p->token) == p->token) {
			*val &= ~p->token;
			return p->pattern;
		}
		p++;
	}

out:
	return NULL;
}

/* ---------------------------------------------------------------------- */

static match_table_t brperm = {
	{AuBrPerm_RO, AUFS_BRPERM_RO},
	{AuBrPerm_RW, AUFS_BRPERM_RW},
	{0, NULL}
};

static match_table_t brattr = {
	/* ro/rr branch */
	{AuBrRAttr_WH, AUFS_BRRATTR_WH},

	/* rw branch */
	{AuBrWAttr_NoLinkWH, AUFS_BRWATTR_NLWH},

	{0, NULL}
};

static int br_attr_val(char *str, match_table_t table, substring_t args[])
{
	int attr, v;
	char *p;

	attr = 0;
	do {
		p = strchr(str, '+');
		if (p)
			*p = 0;
		v = match_token(str, table, args);
		if (v)
			attr |= v;
		else {
			if (p)
				*p = '+';
			pr_warn("ignored branch attribute %s\n", str);
			break;
		}
		if (p)
			str = p + 1;
	} while (p);

	return attr;
}

static int au_do_optstr_br_attr(au_br_perm_str_t *str, int perm)
{
	int sz;
	const char *p;
	char *q;

	q = str->a;
	*q = 0;
	p = au_optstr(&perm, brattr);
	if (p) {
		sz = strlen(p);
		memcpy(q, p, sz + 1);
		q += sz;
	} else
		goto out;

	do {
		p = au_optstr(&perm, brattr);
		if (p) {
			*q++ = '+';
			sz = strlen(p);
			memcpy(q, p, sz + 1);
			q += sz;
		}
	} while (p);

out:
	return q - str->a;
}

static int noinline_for_stack br_perm_val(char *perm)
{
	int val, bad, sz;
	char *p;
	substring_t args[MAX_OPT_ARGS];
	au_br_perm_str_t attr;

	p = strchr(perm, '+');
	if (p)
		*p = 0;
	val = match_token(perm, brperm, args);
	if (!val) {
		if (p)
			*p = '+';
		pr_warn("ignored branch permission %s\n", perm);
		val = AuBrPerm_RO;
		goto out;
	}
	if (!p)
		goto out;

	val |= br_attr_val(p + 1, brattr, args);

	bad = 0;
	switch (val & AuBrPerm_Mask) {
	case AuBrPerm_RO:
		bad = val & AuBrWAttr_Mask;
		val &= ~AuBrWAttr_Mask;
		break;
	case AuBrPerm_RW:
		bad = val & AuBrRAttr_Mask;
		val &= ~AuBrRAttr_Mask;
		break;
	}
	if (unlikely(bad)) {
		sz = au_do_optstr_br_attr(&attr, bad);
		AuDebugOn(!sz);
		pr_warn("ignored branch attribute %s\n", attr.a);
	}

out:
	return val;
}

void au_optstr_br_perm(au_br_perm_str_t *str, int perm)
{
	au_br_perm_str_t attr;
	const char *p;
	char *q;
	int sz;

	q = str->a;
	p = au_optstr(&perm, brperm);
	AuDebugOn(!p || !*p);
	sz = strlen(p);
	memcpy(q, p, sz + 1);
	q += sz;

	sz = au_do_optstr_br_attr(&attr, perm);
	if (sz) {
		*q++ = '+';
		memcpy(q, attr.a, sz + 1);
	}

	AuDebugOn(strlen(str->a) >= sizeof(str->a));
}

/* ---------------------------------------------------------------------- */

static match_table_t au_wbr_create_policy = {
	{AuWbrCreate_TDP, "tdp"},
	{AuWbrCreate_TDP, "top-down-parent"},
	{AuWbrCreate_RR, "rr"},
	{AuWbrCreate_RR, "round-robin"},
	{AuWbrCreate_MFS, "mfs"},
	{AuWbrCreate_MFS, "most-free-space"},
	{AuWbrCreate_MFSV, "mfs:%d"},
	{AuWbrCreate_MFSV, "most-free-space:%d"},

	{AuWbrCreate_MFSRR, "mfsrr:%d"},
	{AuWbrCreate_MFSRRV, "mfsrr:%d:%d"},
	{AuWbrCreate_PMFS, "pmfs"},
	{AuWbrCreate_PMFSV, "pmfs:%d"},
	{AuWbrCreate_PMFSRR, "pmfsrr:%d"},
	{AuWbrCreate_PMFSRRV, "pmfsrr:%d:%d"},

	{-1, NULL}
};

/*
 * cf. linux/lib/parser.c and cmdline.c
 * gave up calling memparse() since it uses simple_strtoull() instead of
 * kstrto...().
 */
static int noinline_for_stack
au_match_ull(substring_t *s, unsigned long long *result)
{
	int err;
	unsigned int len;
	char a[32];

	err = -ERANGE;
	len = s->to - s->from;
	if (len + 1 <= sizeof(a)) {
		memcpy(a, s->from, len);
		a[len] = '\0';
		err = kstrtoull(a, 0, result);
	}
	return err;
}

static int au_wbr_mfs_wmark(substring_t *arg, char *str,
			    struct au_opt_wbr_create *create)
{
	int err;
	unsigned long long ull;

	err = 0;
	if (!au_match_ull(arg, &ull))
		create->mfsrr_watermark = ull;
	else {
		pr_err("bad integer in %s\n", str);
		err = -EINVAL;
	}

	return err;
}

static int au_wbr_mfs_sec(substring_t *arg, char *str,
			  struct au_opt_wbr_create *create)
{
	int n, err;

	err = 0;
	if (!match_int(arg, &n) && 0 <= n && n <= AUFS_MFS_MAX_SEC)
		create->mfs_second = n;
	else {
		pr_err("bad integer in %s\n", str);
		err = -EINVAL;
	}

	return err;
}

static int noinline_for_stack
au_wbr_create_val(char *str, struct au_opt_wbr_create *create)
{
	int err, e;
	substring_t args[MAX_OPT_ARGS];

	err = match_token(str, au_wbr_create_policy, args);
	create->wbr_create = err;
	switch (err) {
	case AuWbrCreate_MFSRRV:
	case AuWbrCreate_PMFSRRV:
		e = au_wbr_mfs_wmark(&args[0], str, create);
		if (!e)
			e = au_wbr_mfs_sec(&args[1], str, create);
		if (unlikely(e))
			err = e;
		break;
	case AuWbrCreate_MFSRR:
	case AuWbrCreate_PMFSRR:
		e = au_wbr_mfs_wmark(&args[0], str, create);
		if (unlikely(e)) {
			err = e;
			break;
		}
		/*FALLTHROUGH*/
	case AuWbrCreate_MFS:
	case AuWbrCreate_PMFS:
		create->mfs_second = AUFS_MFS_DEF_SEC;
		break;
	case AuWbrCreate_MFSV:
	case AuWbrCreate_PMFSV:
		e = au_wbr_mfs_sec(&args[0], str, create);
		if (unlikely(e))
			err = e;
		break;
	}

	return err;
}

const char *au_optstr_wbr_create(int wbr_create)
{
	return au_parser_pattern(wbr_create, au_wbr_create_policy);
}

static match_table_t au_wbr_copyup_policy = {
	{AuWbrCopyup_TDP, "tdp"},
	{AuWbrCopyup_TDP, "top-down-parent"},
	{AuWbrCopyup_BUP, "bup"},
	{AuWbrCopyup_BUP, "bottom-up-parent"},
	{AuWbrCopyup_BU, "bu"},
	{AuWbrCopyup_BU, "bottom-up"},
	{-1, NULL}
};

static int noinline_for_stack au_wbr_copyup_val(char *str)
{
	substring_t args[MAX_OPT_ARGS];

	return match_token(str, au_wbr_copyup_policy, args);
}

const char *au_optstr_wbr_copyup(int wbr_copyup)
{
	return au_parser_pattern(wbr_copyup, au_wbr_copyup_policy);
}

/* ---------------------------------------------------------------------- */

static const int lkup_dirflags = LOOKUP_FOLLOW | LOOKUP_DIRECTORY;

static void dump_opts(struct au_opts *opts)
{
#ifdef CONFIG_AUFS_DEBUG
	/* reduce stack space */
	union {
		struct au_opt_add *add;
		struct au_opt_xino *xino;
		struct au_opt_wbr_create *create;
	} u;
	struct au_opt *opt;

	opt = opts->opt;
	while (opt->type != Opt_tail) {
		switch (opt->type) {
		case Opt_add:
			u.add = &opt->add;
			AuDbg("add {b%d, %s, 0x%x, %p}\n",
				  u.add->bindex, u.add->pathname, u.add->perm,
				  u.add->path.dentry);
			break;
		case Opt_rdcache:
			AuDbg("rdcache %d\n", opt->rdcache);
			break;
		case Opt_rdblk:
			AuDbg("rdblk %u\n", opt->rdblk);
			break;
		case Opt_rdblk_def:
			AuDbg("rdblk_def\n");
			break;
		case Opt_rdhash:
			AuDbg("rdhash %u\n", opt->rdhash);
			break;
		case Opt_rdhash_def:
			AuDbg("rdhash_def\n");
			break;
		case Opt_xino:
			u.xino = &opt->xino;
			AuDbg("xino {%s %pD}\n", u.xino->path, u.xino->file);
			break;
		case Opt_noxino:
			AuLabel(noxino);
			break;
		case Opt_plink:
			AuLabel(plink);
			break;
		case Opt_noplink:
			AuLabel(noplink);
			break;
		case Opt_list_plink:
			AuLabel(list_plink);
			break;
		case Opt_wbr_create:
			u.create = &opt->wbr_create;
			AuDbg("create %d, %s\n", u.create->wbr_create,
				  au_optstr_wbr_create(u.create->wbr_create));
			switch (u.create->wbr_create) {
			case AuWbrCreate_MFSV:
			case AuWbrCreate_PMFSV:
				AuDbg("%d sec\n", u.create->mfs_second);
				break;
			case AuWbrCreate_MFSRR:
				AuDbg("%llu watermark\n",
					  u.create->mfsrr_watermark);
				break;
			case AuWbrCreate_MFSRRV:
			case AuWbrCreate_PMFSRRV:
				AuDbg("%llu watermark, %d sec\n",
					  u.create->mfsrr_watermark,
					  u.create->mfs_second);
				break;
			}
			break;
		case Opt_wbr_copyup:
			AuDbg("copyup %d, %s\n", opt->wbr_copyup,
				  au_optstr_wbr_copyup(opt->wbr_copyup));
			break;
		default:
			BUG();
		}
		opt++;
	}
#endif
}

void au_opts_free(struct au_opts *opts)
{
	struct au_opt *opt;

	opt = opts->opt;
	while (opt->type != Opt_tail) {
		switch (opt->type) {
		case Opt_add:
			path_put(&opt->add.path);
			break;
		case Opt_xino:
			fput(opt->xino.file);
			break;
		}
		opt++;
	}
}

static int opt_add(struct au_opt *opt, char *opt_str, unsigned long sb_flags,
		   aufs_bindex_t bindex)
{
	int err;
	struct au_opt_add *add = &opt->add;
	char *p;

	add->bindex = bindex;
	add->perm = AuBrPerm_RO;
	add->pathname = opt_str;
	p = strchr(opt_str, '=');
	if (p) {
		*p++ = 0;
		if (*p)
			add->perm = br_perm_val(p);
	}

	err = vfsub_kern_path(add->pathname, lkup_dirflags, &add->path);
	if (!err) {
		if (!p) {
			add->perm = AuBrPerm_RO;
			if (!bindex && !(sb_flags & MS_RDONLY))
				add->perm = AuBrPerm_RW;
		}
		opt->type = Opt_add;
		goto out;
	}
	pr_err("lookup failed %s (%d)\n", add->pathname, err);
	err = -EINVAL;

out:
	return err;
}

static int au_opts_parse_xino(struct super_block *sb, struct au_opt_xino *xino,
			      substring_t args[])
{
	int err;
	struct file *file;

	file = au_xino_create(sb, args[0].from, /*silent*/0);
	err = PTR_ERR(file);
	if (IS_ERR(file))
		goto out;

	err = -EINVAL;
	if (unlikely(file->f_path.dentry->d_sb == sb)) {
		fput(file);
		pr_err("%s must be outside\n", args[0].from);
		goto out;
	}

	err = 0;
	xino->file = file;
	xino->path = args[0].from;

out:
	return err;
}

/* called without aufs lock */
int au_opts_parse(struct super_block *sb, char *str, struct au_opts *opts)
{
	int err, n, token;
	aufs_bindex_t bindex;
	unsigned char skipped;
	struct dentry *root;
	struct au_opt *opt, *opt_tail;
	char *opt_str;
	/* reduce the stack space */
	union {
		struct au_opt_wbr_create *create;
		/* will be added more later */
	} u;
	struct {
		substring_t args[MAX_OPT_ARGS];
	} *a;

	err = -ENOMEM;
	a = kmalloc(sizeof(*a), GFP_NOFS);
	if (unlikely(!a))
		goto out;

	root = sb->s_root;
	err = 0;
	bindex = 0;
	opt = opts->opt;
	opt_tail = opt + opts->max_opt - 1;
	opt->type = Opt_tail;
	while (!err && (opt_str = strsep(&str, ",")) && *opt_str) {
		err = -EINVAL;
		skipped = 0;
		token = match_token(opt_str, options, a->args);
		switch (token) {
		case Opt_br:
			err = 0;
			while (!err && (opt_str = strsep(&a->args[0].from, ":"))
			       && *opt_str) {
				err = opt_add(opt, opt_str, opts->sb_flags,
					      bindex++);
				if (unlikely(!err && ++opt > opt_tail)) {
					err = -E2BIG;
					break;
				}
				opt->type = Opt_tail;
				skipped = 1;
			}
			break;
		case Opt_add:
			if (unlikely(match_int(&a->args[0], &n))) {
				pr_err("bad integer in %s\n", opt_str);
				break;
			}
			bindex = n;
			err = opt_add(opt, a->args[1].from, opts->sb_flags,
				      bindex);
			if (!err)
				opt->type = token;
			break;
		case Opt_xino:
			err = au_opts_parse_xino(sb, &opt->xino, a->args);
			if (!err)
				opt->type = token;
			break;

		case Opt_rdcache:
			if (unlikely(match_int(&a->args[0], &n))) {
				pr_err("bad integer in %s\n", opt_str);
				break;
			}
			if (unlikely(n > AUFS_RDCACHE_MAX)) {
				pr_err("rdcache must be smaller than %d\n",
				       AUFS_RDCACHE_MAX);
				break;
			}
			opt->rdcache = n;
			err = 0;
			opt->type = token;
			break;
		case Opt_rdblk:
			if (unlikely(match_int(&a->args[0], &n)
				     || n < 0
				     || n > KMALLOC_MAX_SIZE)) {
				pr_err("bad integer in %s\n", opt_str);
				break;
			}
			if (unlikely(n && n < NAME_MAX)) {
				pr_err("rdblk must be larger than %d\n",
				       NAME_MAX);
				break;
			}
			opt->rdblk = n;
			err = 0;
			opt->type = token;
			break;
		case Opt_rdhash:
			if (unlikely(match_int(&a->args[0], &n)
				     || n < 0
				     || n * sizeof(struct hlist_head)
				     > KMALLOC_MAX_SIZE)) {
				pr_err("bad integer in %s\n", opt_str);
				break;
			}
			opt->rdhash = n;
			err = 0;
			opt->type = token;
			break;

		case Opt_noxino:
		case Opt_plink:
		case Opt_noplink:
		case Opt_list_plink:
		case Opt_rdblk_def:
		case Opt_rdhash_def:
			err = 0;
			opt->type = token;
			break;

		case Opt_wbr_create:
			u.create = &opt->wbr_create;
			u.create->wbr_create
				= au_wbr_create_val(a->args[0].from, u.create);
			if (u.create->wbr_create >= 0) {
				err = 0;
				opt->type = token;
			} else
				pr_err("wrong value, %s\n", opt_str);
			break;
		case Opt_wbr_copyup:
			opt->wbr_copyup = au_wbr_copyup_val(a->args[0].from);
			if (opt->wbr_copyup >= 0) {
				err = 0;
				opt->type = token;
			} else
				pr_err("wrong value, %s\n", opt_str);
			break;

		case Opt_ignore:
			pr_warn("ignored %s\n", opt_str);
			/*FALLTHROUGH*/
		case Opt_ignore_silent:
			skipped = 1;
			err = 0;
			break;
		case Opt_err:
			pr_err("unknown option %s\n", opt_str);
			break;
		}

		if (!err && !skipped) {
			if (unlikely(++opt > opt_tail)) {
				err = -E2BIG;
				opt--;
				opt->type = Opt_tail;
				break;
			}
			opt->type = Opt_tail;
		}
	}

	kfree(a);
	dump_opts(opts);
	if (unlikely(err))
		au_opts_free(opts);

out:
	return err;
}

static int au_opt_wbr_create(struct super_block *sb,
			     struct au_opt_wbr_create *create)
{
	int err;
	struct au_sbinfo *sbinfo;

	SiMustWriteLock(sb);

	err = 1; /* handled */
	sbinfo = au_sbi(sb);
	if (sbinfo->si_wbr_create_ops->fin) {
		err = sbinfo->si_wbr_create_ops->fin(sb);
		if (!err)
			err = 1;
	}

	sbinfo->si_wbr_create = create->wbr_create;
	sbinfo->si_wbr_create_ops = au_wbr_create_ops + create->wbr_create;
	switch (create->wbr_create) {
	case AuWbrCreate_MFSRRV:
	case AuWbrCreate_MFSRR:
	case AuWbrCreate_PMFSRR:
	case AuWbrCreate_PMFSRRV:
		sbinfo->si_wbr_mfs.mfsrr_watermark = create->mfsrr_watermark;
		/*FALLTHROUGH*/
	case AuWbrCreate_MFS:
	case AuWbrCreate_MFSV:
	case AuWbrCreate_PMFS:
	case AuWbrCreate_PMFSV:
		sbinfo->si_wbr_mfs.mfs_expire
			= msecs_to_jiffies(create->mfs_second * MSEC_PER_SEC);
		break;
	}

	if (sbinfo->si_wbr_create_ops->init)
		sbinfo->si_wbr_create_ops->init(sb); /* ignore */

	return err;
}

/*
 * returns,
 * plus: processed without an error
 * zero: unprocessed
 */
static int au_opt_simple(struct super_block *sb, struct au_opt *opt,
			 struct au_opts *opts)
{
	int err;
	struct au_sbinfo *sbinfo;

	SiMustWriteLock(sb);

	err = 1; /* handled */
	sbinfo = au_sbi(sb);
	switch (opt->type) {
	case Opt_plink:
		au_opt_set(sbinfo->si_mntflags, PLINK);
		break;
	case Opt_noplink:
		if (au_opt_test(sbinfo->si_mntflags, PLINK))
			au_plink_put(sb, /*verbose*/1);
		au_opt_clr(sbinfo->si_mntflags, PLINK);
		break;
	case Opt_list_plink:
		if (au_opt_test(sbinfo->si_mntflags, PLINK))
			au_plink_list(sb);
		break;

	case Opt_wbr_create:
		err = au_opt_wbr_create(sb, &opt->wbr_create);
		break;
	case Opt_wbr_copyup:
		sbinfo->si_wbr_copyup = opt->wbr_copyup;
		sbinfo->si_wbr_copyup_ops = au_wbr_copyup_ops + opt->wbr_copyup;
		break;

	case Opt_rdcache:
		sbinfo->si_rdcache
			= msecs_to_jiffies(opt->rdcache * MSEC_PER_SEC);
		break;
	case Opt_rdblk:
		sbinfo->si_rdblk = opt->rdblk;
		break;
	case Opt_rdblk_def:
		sbinfo->si_rdblk = AUFS_RDBLK_DEF;
		break;
	case Opt_rdhash:
		sbinfo->si_rdhash = opt->rdhash;
		break;
	case Opt_rdhash_def:
		sbinfo->si_rdhash = AUFS_RDHASH_DEF;
		break;

	default:
		err = 0;
		break;
	}

	return err;
}

/*
 * returns tri-state.
 * plus: processed without an error
 * zero: unprocessed
 * minus: error
 */
static int au_opt_br(struct super_block *sb, struct au_opt *opt,
		     struct au_opts *opts)
{
	int err;

	err = 0;
	switch (opt->type) {
	case Opt_add:
		err = au_br_add(sb, &opt->add);
		if (!err) {
			err = 1;
			/* au_fset_opts(opts->flags, REFRESH); re-commit later */
		}
		break;
	}

	return err;
}

static int au_opt_xino(struct super_block *sb, struct au_opt *opt,
		       struct au_opt_xino **opt_xino,
		       struct au_opts *opts)
{
	int err;
	aufs_bindex_t bend, bindex;
	struct dentry *root, *parent, *h_root;

	err = 0;
	switch (opt->type) {
	case Opt_xino:
		err = au_xino_set(sb, &opt->xino);
		if (unlikely(err))
			break;

		*opt_xino = &opt->xino;
		au_xino_brid_set(sb, -1);

		/* safe d_parent access */
		parent = opt->xino.file->f_path.dentry->d_parent;
		root = sb->s_root;
		bend = au_sbend(sb);
		for (bindex = 0; bindex <= bend; bindex++) {
			h_root = au_h_dptr(root, bindex);
			if (h_root == parent) {
				au_xino_brid_set(sb, au_sbr_id(sb, bindex));
				break;
			}
		}
		break;

	case Opt_noxino:
		au_xino_clr(sb);
		au_xino_brid_set(sb, -1);
		*opt_xino = (void *)-1;
		break;
	}

	return err;
}

int au_opts_verify(struct super_block *sb, unsigned long sb_flags,
		   unsigned int pending)
{
	int err;
	aufs_bindex_t bindex, bend;
	unsigned char do_plink, skip, do_free;
	struct au_branch *br;
	struct au_wbr *wbr;
	struct dentry *root;
	struct inode *dir, *h_dir;
	struct au_sbinfo *sbinfo;
	struct au_hinode *hdir;

	SiMustAnyLock(sb);

	sbinfo = au_sbi(sb);

	if (!(sb_flags & MS_RDONLY)) {
		if (unlikely(!au_br_writable(au_sbr_perm(sb, 0))))
			pr_warn("first branch should be rw\n");
	}

	err = 0;
	root = sb->s_root;
	dir = root->d_inode;
	do_plink = !!au_opt_test(sbinfo->si_mntflags, PLINK);
	bend = au_sbend(sb);
	for (bindex = 0; !err && bindex <= bend; bindex++) {
		skip = 0;
		h_dir = au_h_iptr(dir, bindex);
		br = au_sbr(sb, bindex);
		do_free = 0;

		wbr = br->br_wbr;
		if (wbr)
			wbr_wh_read_lock(wbr);

		if (!au_br_writable(br->br_perm)) {
			do_free = !!wbr;
			skip = (!wbr
				|| (!wbr->wbr_whbase
				    && !wbr->wbr_plink
				    && !wbr->wbr_orph));
		} else if (!au_br_wh_linkable(br->br_perm)) {
			/* skip = (!br->br_whbase && !br->br_orph); */
			skip = (!wbr || !wbr->wbr_whbase);
			if (skip && wbr) {
				if (do_plink)
					skip = !!wbr->wbr_plink;
				else
					skip = !wbr->wbr_plink;
			}
		} else {
			/* skip = (br->br_whbase && br->br_ohph); */
			skip = (wbr && wbr->wbr_whbase);
			if (skip) {
				if (do_plink)
					skip = !!wbr->wbr_plink;
				else
					skip = !wbr->wbr_plink;
			}
		}
		if (wbr)
			wbr_wh_read_unlock(wbr);

		if (skip)
			continue;

		hdir = au_hi(dir, bindex);
		mutex_lock_nested(&hdir->hi_inode->i_mutex, AuLsc_I_PARENT);
		if (wbr)
			wbr_wh_write_lock(wbr);
		err = au_wh_init(br, sb);
		if (wbr)
			wbr_wh_write_unlock(wbr);
		mutex_unlock(&hdir->hi_inode->i_mutex);

		if (!err && do_free) {
			kfree(wbr);
			br->br_wbr = NULL;
		}
	}

	return err;
}

int au_opts_mount(struct super_block *sb, struct au_opts *opts)
{
	int err;
	unsigned int tmp;
	aufs_bindex_t bend;
	struct au_opt *opt;
	struct au_opt_xino *opt_xino, xino;
	struct au_sbinfo *sbinfo;

	SiMustWriteLock(sb);

	err = 0;
	opt_xino = NULL;
	opt = opts->opt;
	while (err >= 0 && opt->type != Opt_tail)
		err = au_opt_simple(sb, opt++, opts);
	if (err > 0)
		err = 0;
	else if (unlikely(err < 0))
		goto out;

	/* disable xino temporary */
	sbinfo = au_sbi(sb);
	tmp = sbinfo->si_mntflags;
	au_opt_clr(sbinfo->si_mntflags, XINO);

	opt = opts->opt;
	while (err >= 0 && opt->type != Opt_tail)
		err = au_opt_br(sb, opt++, opts);
	if (err > 0)
		err = 0;
	else if (unlikely(err < 0))
		goto out;

	bend = au_sbend(sb);
	if (unlikely(bend < 0)) {
		err = -EINVAL;
		pr_err("no branches\n");
		goto out;
	}

	if (au_opt_test(tmp, XINO))
		au_opt_set(sbinfo->si_mntflags, XINO);
	opt = opts->opt;
	while (!err && opt->type != Opt_tail)
		err = au_opt_xino(sb, opt++, &opt_xino, opts);
	if (unlikely(err))
		goto out;

	err = au_opts_verify(sb, sb->s_flags, tmp);
	if (unlikely(err))
		goto out;

	/* restore xino */
	if (au_opt_test(tmp, XINO) && !opt_xino) {
		xino.file = au_xino_def(sb);
		err = PTR_ERR(xino.file);
		if (IS_ERR(xino.file))
			goto out;

		err = au_xino_set(sb, &xino);
		fput(xino.file);
		if (unlikely(err))
			goto out;
	}

	bend = au_sbend(sb);

out:
	return err;
}
