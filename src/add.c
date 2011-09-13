/* add.c - Alpine Package Keeper (APK)
 *
 * Copyright (C) 2005-2008 Natanael Copa <n@tanael.org>
 * Copyright (C) 2008-2011 Timo Teräs <timo.teras@iki.fi>
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation. See http://www.gnu.org/ for details.
 */

#include <errno.h>
#include <stdio.h>
#include "apk_applet.h"
#include "apk_database.h"
#include "apk_print.h"
#include "apk_solver.h"

struct add_ctx {
	const char *virtpkg;
	unsigned short solver_flags;
};

static int add_parse(void *ctx, struct apk_db_options *dbopts,
		     int optch, int optindex, const char *optarg)
{
	struct add_ctx *actx = (struct add_ctx *) ctx;

	switch (optch) {
	case 0x10000:
		dbopts->open_flags |= APK_OPENF_CREATE;
		break;
	case 'u':
		actx->solver_flags |= APK_SOLVERF_UPGRADE;
		break;
	case 't':
		actx->virtpkg = optarg;
		break;
	default:
		return -1;
	}
	return 0;
}

static int non_repository_check(struct apk_database *db)
{
	if (apk_flags & APK_FORCE)
		return 0;
	if (apk_db_cache_active(db))
		return 0;
	if (apk_db_permanent(db))
		return 0;

	apk_error("You tried to add a non-repository package to system, "
		  "but it would be lost on next reboot. Enable package caching "
		  "(apk cache --help) or use --force if you know what you are "
		  "doing.");
	return 1;
}

static int add_main(void *ctx, struct apk_database *db, int argc, char **argv)
{
	struct add_ctx *actx = (struct add_ctx *) ctx;
	struct apk_package *virtpkg = NULL;
	struct apk_dependency virtdep;
	struct apk_dependency_array *world = NULL;
	int i, r = 0;

	apk_dependency_array_copy(&world, db->world);

	if (actx->virtpkg) {
		if (non_repository_check(db))
			return -1;

		virtpkg = apk_pkg_new();
		if (virtpkg == NULL) {
			apk_error("Failed to allocate virtual meta package");
			return -1;
		}
		virtpkg->name = apk_db_get_name(db, APK_BLOB_STR(actx->virtpkg));
		apk_blob_checksum(APK_BLOB_STR(virtpkg->name->name),
				  apk_checksum_default(), &virtpkg->csum);
		virtpkg->version = apk_blob_atomize(APK_BLOB_STR("0"));
		virtpkg->description = strdup("virtual meta package");
		virtpkg->arch = apk_blob_atomize(APK_BLOB_STR("noarch"));
		apk_dep_from_pkg(&virtdep, db, virtpkg);
		virtpkg = apk_db_pkg_add(db, virtpkg);
	}

	for (i = 0; i < argc; i++) {
		struct apk_dependency dep;

		if (strstr(argv[i], ".apk") != NULL) {
			struct apk_package *pkg = NULL;
			struct apk_sign_ctx sctx;

			if (non_repository_check(db))
				return -1;

			apk_sign_ctx_init(&sctx, APK_SIGN_VERIFY_AND_GENERATE,
					  NULL, db->keys_fd);
			r = apk_pkg_read(db, argv[i], &sctx, &pkg);
			apk_sign_ctx_free(&sctx);
			if (r != 0) {
				apk_error("%s: %s", argv[i], apk_error_str(r));
				return -1;
			}
			apk_dep_from_pkg(&dep, db, pkg);
		} else {
			r = apk_dep_from_blob(&dep, db, APK_BLOB_STR(argv[i]));
			if (r != 0)
				return -1;
		}

		if (virtpkg == NULL) {
			apk_deps_add(&world, &dep);
			apk_solver_set_name_flags(dep.name, actx->solver_flags);
		} else {
			apk_deps_add(&virtpkg->depends, &dep);
		}
	}
	if (virtpkg) {
		apk_deps_add(&world, &virtdep);
		apk_solver_set_name_flags(virtdep.name, actx->solver_flags);
	}

	r = apk_solver_commit(db, 0, world);
	apk_dependency_array_free(&world);

	return r;
}

static struct apk_option add_options[] = {
	{ 0x10000,	"initdb",	"Initialize database" },
	{ 'u',		"upgrade",	"Prefer to upgrade package" },
	{ 't',		"virtual",
	  "Instead of adding all the packages to 'world', create a new virtual "
	  "package with the listed dependencies and add that to 'world'. The "
	  "actions of the command are easily reverted by deleting the virtual "
	  "package.", required_argument, "NAME" },
};

static struct apk_applet apk_add = {
	.name = "add",
	.help = "Add (or update) PACKAGEs to main dependencies and install "
		"them, while ensuring that all dependencies are met.",
	.arguments = "PACKAGE...",
	.open_flags = APK_OPENF_WRITE,
	.context_size = sizeof(struct add_ctx),
	.num_options = ARRAY_SIZE(add_options),
	.options = add_options,
	.parse = add_parse,
	.main = add_main,
};

APK_DEFINE_APPLET(apk_add);
