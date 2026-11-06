#!/usr/bin/env bash
#
# Build pg_opencc against PGDG PostgreSQL and run the regression tests.
# Runs as root inside a Debian/Ubuntu or EL (Rocky) container:
#
#   ci/test.sh <pg-major>
#
set -euo pipefail
PG=${1:-18}
cd "$(dirname "$0")/.."
. /etc/os-release

if command -v apt-get >/dev/null 2>&1; then
	export DEBIAN_FRONTEND=noninteractive
	apt-get update -qq
	apt-get install -y -qq --no-install-recommends ca-certificates curl gnupg lsb-release postgresql-common >/dev/null
	echo 'create_main_cluster = false' >> /etc/postgresql-common/createcluster.conf
	/usr/share/postgresql-common/pgdg/apt.postgresql.org.sh -y >/dev/null
	apt-get install -y -qq --no-install-recommends "postgresql-$PG" "postgresql-server-dev-$PG" \
		cmake g++ make python3 diffutils >/dev/null
	PGBIN=/usr/lib/postgresql/$PG/bin
else
	EL=${VERSION_ID%%.*}
	dnf install -y -q dnf-plugins-core
	if [ "$EL" = 8 ]; then dnf config-manager --set-enabled powertools; else dnf config-manager --set-enabled crb; fi
	dnf install -y -q "https://download.postgresql.org/pub/repos/yum/reporpms/EL-$EL-$(uname -m)/pgdg-redhat-repo-latest.noarch.rpm"
	if [ "$EL" -lt 10 ]; then dnf -qy module disable postgresql; fi
	dnf install -y -q "postgresql$PG-server" "postgresql$PG-devel" cmake gcc-c++ make python3 redhat-rpm-config diffutils
	PGBIN=/usr/pgsql-$PG/bin
fi

echo "== $PRETTY_NAME, $(uname -m), $("$PGBIN/pg_config" --version), $(c++ --version | head -1), $(cmake --version | head -1)"
MAKEARGS=(PG_CONFIG="$PGBIN/pg_config" with_llvm=no)
make -j"$(nproc)" "${MAKEARGS[@]}"
make install "${MAKEARGS[@]}"

DATA=$(mktemp -d)
chown postgres "$DATA"
su postgres -c "$PGBIN/initdb -D $DATA/pgdata -E UTF8 --locale=C.UTF-8 -A trust" >/dev/null
su postgres -c "$PGBIN/pg_ctl -D $DATA/pgdata -l $DATA/log -o '-p 5440 -k /tmp -c listen_addresses=' -w start" >/dev/null

if ! make installcheck "${MAKEARGS[@]}" PGHOST=/tmp PGPORT=5440 PGUSER=postgres; then
	cat build/test/regression.diffs || true
	exit 1
fi

psql -X -h /tmp -p 5440 -U postgres -d postgres -Atc \
	"CREATE EXTENSION opencc; SELECT format('opencc.version: %s / OpenCC %s / %s', extension, opencc, dictionary) FROM opencc.version();"
case "$(uname -s)" in
Linux)
	echo "exported symbols: $(nm -D --defined-only "$("$PGBIN/pg_config" --pkglibdir)/opencc.so" | wc -l)"
	echo "shared libraries:"; ldd "$("$PGBIN/pg_config" --pkglibdir)/opencc.so"
	;;
esac
