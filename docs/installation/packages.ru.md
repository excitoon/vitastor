[Документация](../../README-ru.md#документация) → Установка → Установка из пакетов

-----

[Read in English](packages.en.md)

# Установка из пакетов

## Debian

- Добавьте ключ репозитория Vitastor:
  `wget https://vitastor.io/debian/pubkey.gpg -O /etc/apt/trusted.gpg.d/vitastor.gpg`
- Добавьте репозиторий Vitastor в /etc/apt/sources.list:
  - Debian 11 (Bullseye/Sid): `deb https://vitastor.io/debian bullseye main`
  - Debian 10 (Buster): `deb https://vitastor.io/debian buster main`
- Для Debian 10 (Buster) также включите репозиторий backports:
  `deb http://deb.debian.org/debian buster-backports main`
- Установите пакеты: `apt update; apt install vitastor lp-solve etcd linux-image-amd64 qemu`

## CentOS

- Добавьте в систему репозиторий Vitastor:
  - CentOS 7: `yum install https://vitastor.io/rpms/centos/7/vitastor-release.rpm`
  - CentOS 8: `dnf install https://vitastor.io/rpms/centos/8/vitastor-release.rpm`
- Включите EPEL: `yum/dnf install epel-release`
- Включите дополнительные репозитории CentOS:
  - CentOS 7: `yum install centos-release-scl`
  - CentOS 8: `dnf install centos-release-advanced-virtualization`
- Включите elrepo-kernel:
  - CentOS 7: `yum install https://www.elrepo.org/elrepo-release-7.el7.elrepo.noarch.rpm`
  - CentOS 8: `dnf install https://www.elrepo.org/elrepo-release-8.el8.elrepo.noarch.rpm`
- Установите пакеты: `yum/dnf install vitastor lpsolve etcd kernel-ml qemu-kvm`

## Установочные требования

- Ядро Linux 5.4 или новее, для поддержки io_uring. Рекомендуется даже 5.8,
  так как io_uring - относительно новый интерфейс и в версиях до 5.8 встречались
  некоторые баги, например, зависание с io_uring и контроллером HP SmartArray
- liburing 0.4 или новее
- lp_solve
- etcd 3.4.15 или новее. Более старые версии не будут работать из-за разных багов,
  например, [#12402](https://github.com/etcd-io/etcd/pull/12402).
- node.js 10 или новее
