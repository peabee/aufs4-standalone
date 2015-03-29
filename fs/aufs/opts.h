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

#ifndef __AUFS_OPTS_H__
#define __AUFS_OPTS_H__

#ifdef __KERNEL__

#include <linux/path.h>

struct file;
struct super_block;

/* ---------------------------------------------------------------------- */

/* mount flags */
#define AuOpt_XINO		1		/* external inode number bitmap
						   and translation table */
#define AuOpt_UDBA_NONE		(1 << 2)	/* users direct branch access */
#define AuOpt_UDBA_REVAL	(1 << 3)
#define AuOpt_PLINK		(1 << 6)	/* pseudo-link */

#define AuOpt_Def	(AuOpt_XINO \
			 | AuOpt_UDBA_REVAL \
			 | AuOpt_PLINK)
#define AuOptMask_UDBA	(AuOpt_UDBA_NONE \
			 | AuOpt_UDBA_REVAL)

#define au_opt_test(flags, name)	(flags & AuOpt_##name)
#define au_opt_set(flags, name) do { \
	BUILD_BUG_ON(AuOpt_##name & AuOptMask_UDBA); \
	((flags) |= AuOpt_##name); \
} while (0)
#define au_opt_set_udba(flags, name) do { \
	(flags) &= ~AuOptMask_UDBA; \
	((flags) |= AuOpt_##name); \
} while (0)
#define au_opt_clr(flags, name) do { \
	((flags) &= ~AuOpt_##name); \
} while (0)

static inline unsigned int au_opts_plink(unsigned int mntflags)
{
#ifdef CONFIG_PROC_FS
	return mntflags;
#else
	return mntflags & ~AuOpt_PLINK;
#endif
}

/* ---------------------------------------------------------------------- */

/* policies to select one among multiple writable branches */
enum {
	AuWbrCreate_TDP,	/* top down parent */
	AuWbrCreate_RR,		/* round robin */
	AuWbrCreate_MFS,	/* most free space */
	AuWbrCreate_MFSV,	/* mfs with seconds */
	AuWbrCreate_MFSRR,	/* mfs then rr */
	AuWbrCreate_MFSRRV,	/* mfs then rr with seconds */
	AuWbrCreate_PMFS,	/* parent and mfs */
	AuWbrCreate_PMFSV,	/* parent and mfs with seconds */
	AuWbrCreate_PMFSRR,	/* parent, mfs and round-robin */
	AuWbrCreate_PMFSRRV,	/* plus seconds */

	AuWbrCreate_Def = AuWbrCreate_TDP
};

enum {
	AuWbrCopyup_TDP,	/* top down parent */
	AuWbrCopyup_BUP,	/* bottom up parent */
	AuWbrCopyup_BU,		/* bottom up */

	AuWbrCopyup_Def = AuWbrCopyup_TDP
};

/* ---------------------------------------------------------------------- */

struct au_opt_add {
	aufs_bindex_t	bindex;
	char		*pathname;
	int		perm;
	struct path	path;
};

struct au_opt_xino {
	char		*path;
	struct file	*file;
};

struct au_opt_wbr_create {
	int			wbr_create;
	int			mfs_second;
	unsigned long long	mfsrr_watermark;
};

struct au_opt {
	int type;
	union {
		struct au_opt_xino	xino;
		struct au_opt_add	add;
		int			rdcache;
		unsigned int		rdblk;
		unsigned int		rdhash;
		int			udba;
		struct au_opt_wbr_create wbr_create;
		int			wbr_copyup;
	};
};

/* opts flags */
#define AuOpts_REMOUNT		1
#define AuOpts_REFRESH		(1 << 1)
#define au_ftest_opts(flags, name)	((flags) & AuOpts_##name)
#define au_fset_opts(flags, name) \
	do { (flags) |= AuOpts_##name; } while (0)
#define au_fclr_opts(flags, name) \
	do { (flags) &= ~AuOpts_##name; } while (0)

struct au_opts {
	struct au_opt	*opt;
	int		max_opt;

	unsigned int	given_udba;
	unsigned int	flags;
	unsigned long	sb_flags;
};

/* ---------------------------------------------------------------------- */

/* opts.c */
void au_optstr_br_perm(au_br_perm_str_t *str, int perm);
const char *au_optstr_udba(int udba);
const char *au_optstr_wbr_copyup(int wbr_copyup);
const char *au_optstr_wbr_create(int wbr_create);

void au_opts_free(struct au_opts *opts);
int au_opts_parse(struct super_block *sb, char *str, struct au_opts *opts);
int au_opts_verify(struct super_block *sb, unsigned long sb_flags,
		   unsigned int pending);
int au_opts_mount(struct super_block *sb, struct au_opts *opts);
int au_opts_remount(struct super_block *sb, struct au_opts *opts);

unsigned int au_opt_udba(struct super_block *sb);

#endif /* __KERNEL__ */
#endif /* __AUFS_OPTS_H__ */
