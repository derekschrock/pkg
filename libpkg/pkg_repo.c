#include <err.h>
#include <ctype.h>
#include <assert.h>
#include <errno.h>
#include <libgen.h>
#include <string.h>
#include <unistd.h>

#include "pkg.h"
#include "pkg_event.h"
#include "pkg_private.h"

/*
 * Head of the remote repository tail and initializer
 */
static STAILQ_HEAD(remote_repo, pkg_remote_repo) rrh;
static int rrh_init;

/**
 * Trims any leading and trailing spaces from a string
 * @todo Find a better way to deal with such cases :)
 */
static char * trim_spaces(char *str);

int
pkg_repo_fetch(struct pkg *pkg)
{
	char dest[MAXPATHLEN];
	char cksum[65];
	char *path;
	char *url;
	int retcode = EPKG_OK;

	assert((pkg->type & PKG_REMOTE) == PKG_REMOTE ||
		(pkg->type & PKG_UPGRADE) == PKG_UPGRADE);

	snprintf(dest, sizeof(dest), "%s/%s", pkg_config("PKG_CACHEDIR"),
			 pkg_get(pkg, PKG_REPOPATH));

	/* If it is already in the local cachedir, dont bother to download it */
	if (access(dest, F_OK) == 0)
		goto checksum;

	/* Create the dirs in cachedir */
	if ((path = dirname(dest)) == NULL) {
		EMIT_ERRNO("dirname", dest);
		retcode = EPKG_FATAL;
		goto cleanup;
	}
	if ((retcode = mkdirs(path)) != 0)
		goto cleanup;

	asprintf(&url, "%s/%s", pkg_config("PACKAGESITE"),
			 pkg_get(pkg, PKG_REPOPATH));

	retcode = pkg_fetch_file(url, dest);
	free(url);
	if (retcode != EPKG_OK)
		goto cleanup;

	checksum:
	retcode = sha256_file(dest, cksum);
	if (retcode == EPKG_OK)
		if (strcmp(cksum, pkg_get(pkg, PKG_CKSUM))) {
			EMIT_FAILED_CKSUM(pkg);
			retcode = EPKG_FATAL;
		}

	cleanup:
	if (retcode != EPKG_OK)
		unlink(dest);

	return (retcode);
}

void
pkg_remote_repo_init(void)
{
        STAILQ_INIT(&rrh);
        rrh_init = 0;
}

int
pkg_remote_repo_load(void)
{
        FILE *fp;
        char buf[MAXPATHLEN];
        char *repo_name, *repo_url, *tmp;
        unsigned int count = 0, line = 0;

        if ((fp = fopen("/etc/pkg/repositories", "r")) == NULL) {
		EMIT_ERRNO("fopen", "/etc/pkg/repositories");
		return(EPKG_FATAL);
	}

        while (fgets(buf, MAXPATHLEN, fp)) {
                if (buf[0] == '\n' || buf[0] == '#' || buf[0] == ';')
                        continue;

		repo_name = repo_url = tmp = NULL;

                count = 0;
                line++;

                buf[strlen(buf) - 1] = '\0';
                tmp = buf;

                repo_name = strsep(&tmp, "=");
                repo_url  = strsep(&tmp, "=");

                if ((repo_name == NULL) || (repo_url == NULL)) {
                        warnx("Repository name or URL is missing at line %d (ignoring repository)", line);
                        continue;
                }

                repo_name = trim_spaces(repo_name);
                repo_url  = trim_spaces(repo_url);

                if (tmp) {
                        warnx("Wrong repository format at line %d (ignoring repository)", line);
                        continue;
                }
                
                count++;

                pkg_remote_repo_add(repo_name, repo_url);
        }

        fclose(fp);

        return(EPKG_OK);
}

int
pkg_remote_repo_add(const char *name, const char *url)
{
        struct pkg_remote_repo *newrepo;

        if ((newrepo = calloc(1, sizeof(struct pkg_remote_repo))) == NULL) {
                EMIT_ERRNO("calloc", "");
		return(EPKG_FATAL);
        }

        newrepo->name = strdup(name);
        newrepo->url  = strdup(url);

        assert(newrepo->name != NULL && newrepo->url != NULL);
        
        STAILQ_INSERT_TAIL(&rrh, newrepo, entries);

        return(EPKG_OK);
}

struct pkg_remote_repo *
pkg_remote_repo_next(void)
{
        static struct pkg_remote_repo *next;
        
        if (rrh_init == 0) {
                next = STAILQ_FIRST(&rrh);
                rrh_init = 1;
        } else
                next = STAILQ_NEXT(next, entries);

        return(next);
}

void
pkg_remote_repo_free(void)
{
        struct pkg_remote_repo *n1, *n2;

        n1 = STAILQ_FIRST(&rrh);
        while (n1 != NULL) {
                n2 = STAILQ_NEXT(n1, entries);
                
                if (n1->name != NULL)
                        free(n1->name);
                if (n1->url != NULL)
                        free(n1->url);

                free(n1);
                n1 = n2;
        }
}

void
pkg_remote_repo_reset(void)
{
        rrh_init = 0;
}

char *
trim_spaces(char *str)
{
        char *buf;
        int   i, len;

        buf = str;
        len = strlen(str);
                       
        for (i = 0; i < len && isspace(str[i]); i++, buf++) ;
                for (i = len - 1; i >= 0 && isspace(str[i]); str[i] = '\0', i--) ;

        return(buf);
}
