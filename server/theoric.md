# Resumo de Comandos e Funções de SO

---

## 1. Sistema de ficheiros – funções básicas (Unix)

**Cabeçalhos típicos:** `#include <fcntl.h>`, `#include <unistd.h>`, `#include <sys/stat.h>` :contentReference[oaicite:0]{index=0}  

| Função | Protótipo (simplificado) | O que faz / notas |
|--------|--------------------------|-------------------|
| `open` | `int open(const char *path, int flags, mode_t mode);` | Abre ou cria um ficheiro. Devolve um *file descriptor* (inteiro) usado nas restantes operações. `flags` inclui `O_RDONLY`, `O_WRONLY`, `O_RDWR`, `O_CREAT`, `O_TRUNC`, `O_APPEND`, etc. `mode` define permissões quando há `O_CREAT`. :contentReference[oaicite:1]{index=1} |
| `close` | `int close(int fd);` | Fecha o descritor `fd`, libertando a entrada na tabela de ficheiros abertos. :contentReference[oaicite:2]{index=2} |
| `read` | `ssize_t read(int fd, void *buffer, size_t count);` | Lê até `count` bytes de `fd` para `buffer`. Devolve nº de bytes lidos, `0` em EOF, `-1` em erro. Pode ler menos que `count`. :contentReference[oaicite:3]{index=3} |
| `write` | `ssize_t write(int fd, const void *buffer, size_t count);` | Escreve até `count` bytes de `buffer` para o ficheiro `fd`. Devolve nº de bytes realmente escritos (pode ser < `count`). Não garante flush imediato para disco (há *buffering*). :contentReference[oaicite:4]{index=4} |
| `lseek` | `off_t lseek(int fd, off_t offset, int whence);` | Move o cursor do ficheiro (posição da próxima leitura/escrita). `whence` é `SEEK_SET`, `SEEK_CUR` ou `SEEK_END`. :contentReference[oaicite:5]{index=5} |
| `fsync` | `int fsync(int fd);` | Força escrita em disco de dados e metadados do ficheiro associado a `fd`. :contentReference[oaicite:6]{index=6} |
| `fdatasync` | `int fdatasync(int fd);` | Similar a `fsync`, mas sincroniza dados essenciais (pode ignorar alguns metadados). :contentReference[oaicite:7]{index=7} |
| `sync` | `void sync(void);` | Pede ao kernel que escreva em disco todos os *buffers* sujos. :contentReference[oaicite:8]{index=8} |

---

## 2. Diretórios e gestão de ficheiros

### 2.1 Diretórios

**Cabeçalho principal:** `#include <dirent.h>` :contentReference[oaicite:9]{index=9}  

| Função | Protótipo (simplificado) | O que faz / notas |
|--------|--------------------------|-------------------|
| `opendir` | `DIR *opendir(const char *dirpath);` | Abre um diretório e devolve um *directory stream* (`DIR *`) que será usado por `readdir`. :contentReference[oaicite:10]{index=10} |
| `readdir` | `struct dirent *readdir(DIR *dirp);` | Devolve ponteiro para a próxima entrada do diretório `dirp` (estrutura `struct dirent` com `d_name`, `d_ino`, …). Devolve `NULL` em fim ou erro. :contentReference[oaicite:11]{index=11} |
| `closedir` | `int closedir(DIR *dirp);` | Fecha o *directory stream* aberto com `opendir`. (API padrão POSIX; mesmo que não esteja explicitamente no slide, é a função complementar de `opendir`.) |
| `mkdir` | `int mkdir(const char *path, mode_t mode);` | Cria um diretório novo com permissões `mode`. :contentReference[oaicite:12]{index=12} |
| `rmdir` | `int rmdir(const char *path);` | Remove um diretório **vazio**. :contentReference[oaicite:13]{index=13} |
| `chdir` | `int chdir(const char *path);` | Altera o diretório corrente do processo. :contentReference[oaicite:14]{index=14} |

### 2.2 Atributos de ficheiros

**Cabeçalho:** `#include <sys/stat.h>` :contentReference[oaicite:15]{index=15}  

| Função | Protótipo (simplificado) | O que faz / notas |
|--------|--------------------------|-------------------|
| `stat` | `int stat(const char *pathname, struct stat *buf);` | Devolve em `buf` os atributos do ficheiro (`tamanho`, `permissões`, `nº de links`, `timestamps`, etc.). :contentReference[oaicite:16]{index=16} |
| `fstat` | `int fstat(int fd, struct stat *buf);` | Igual a `stat`, mas usando um descritor de ficheiro já aberto. :contentReference[oaicite:17]{index=17} |
| `lstat` | `int lstat(const char *pathname, struct stat *buf);` | Igual a `stat`, mas se o caminho for um *symbolic link* devolve info sobre o link, não sobre o alvo. :contentReference[oaicite:18]{index=18} |
| `chmod` | `int chmod(const char *pathname, mode_t mode);` | Altera as permissões de acesso do ficheiro para `mode`. :contentReference[oaicite:19]{index=19} |

### 2.3 Gestão de nomes e links

**Cabeçalhos:** `#include <unistd.h>` e/ou `<stdio.h>` conforme o caso. :contentReference[oaicite:20]{index=20}  

| Função | Protótipo (simplificado) | O que faz / notas |
|--------|--------------------------|-------------------|
| `link` | `int link(const char *oldpath, const char *newpath);` | Cria um **hard link**: nova entrada de diretório que aponta para o mesmo i-node. O ficheiro só desaparece quando o último hard link é removido. |
| `symlink` | `int symlink(const char *oldpath, const char *newpath);` | Cria um **symbolic link**: ficheiro especial cujo conteúdo é o caminho para o ficheiro/diretório alvo. :contentReference[oaicite:21]{index=21} |
| `rename` | `int rename(const char *oldpath, const char *newpath);` | Muda o nome ou move um ficheiro/diretório (na mesma partição). |
| `unlink` | `int unlink(const char *path);` | Remove um nome de ficheiro (link). Os dados só são libertados quando não restam links. |

---

## 3. Processos (Unix/POSIX)

**Cabeçalhos típicos:**  
`#include <unistd.h>`  
`#include <sys/types.h>`  
`#include <sys/wait.h>` :contentReference[oaicite:22]{index=22}  

| Função | Protótipo (simplificado) | O que faz / notas |
|--------|--------------------------|-------------------|
| `fork` | `pid_t fork(void);` | Cria um processo filho como cópia do pai. No pai devolve o PID do filho; no filho devolve `0`; em erro devolve `-1`. :contentReference[oaicite:23]{index=23} |
| `execv` | `int execv(const char *path, char *const argv[]);` | Substitui o código/dados do processo atual pelo programa em `path`. Se tiver sucesso, **não retorna**. (Outras variantes: `execl`, `execve`, `execvp`, …) :contentReference[oaicite:24]{index=24} |
| `exit` | `void exit(int status);` | Termina o processo atual e devolve `status` ao pai (recolhido por `wait`/`waitpid`). :contentReference[oaicite:25]{index=25} |
| `_exit` | `void _exit(int status);` | Versão de baixo nível de `exit`: termina o processo sem fazer *flush* da stdio, etc. Útil depois de `fork` em algumas situações. |
| `wait` | `pid_t wait(int *status);` | Bloqueia até terminar **um** filho. Escreve em `*status` informação sobre a terminação (usar macros `WIFEXITED`, `WEXITSTATUS`, etc.). :contentReference[oaicite:26]{index=26} |
| `waitpid` | `pid_t waitpid(pid_t pid, int *status, int options);` | Versão mais flexível de `wait` (escolhe que filho esperar, pode ser não bloqueante, etc.). |
| `setuid` | `int setuid(uid_t uid);` | Muda o UID efetivo do processo (se o processo tiver privilégios). :contentReference[oaicite:27]{index=27} |
| `setgid` | `int setgid(gid_t gid);` | Muda o GID efetivo do processo (mesma ideia que `setuid`). :contentReference[oaicite:28]{index=28} |

---

## 4. Tarefas (threads) POSIX

**Cabeçalho principal:** `#include <pthread.h>` :contentReference[oaicite:29]{index=29}  

| Função | Protótipo (simplificado) | O que faz / notas |
|--------|--------------------------|-------------------|
| `pthread_create` | `int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg);` | Cria uma nova tarefa no mesmo processo. A função `start_routine` é executada na nova thread e recebe `arg` como argumento (tipicamente convertido a/desde `void *`). |
| `pthread_exit` | `void pthread_exit(void *value_ptr);` | Termina a thread chamadora e devolve `value_ptr` como resultado para quem fizer `pthread_join`. |
| `pthread_join` | `int pthread_join(pthread_t thread, void **value_ptr);` | Bloqueia até a thread `thread` terminar. Guarda em `*value_ptr` o valor passado a `pthread_exit`. |

---

## 5. Sincronização – mutexes, trincos de leitura-escrita e variáveis de condição

### 5.1 Mutexes (`pthread_mutex_*`)

**Cabeçalho:** `#include <pthread.h>` :contentReference[oaicite:30]{index=30}  

| Função | Protótipo (simplificado) | O que faz / notas |
|--------|--------------------------|-------------------|
| `pthread_mutex_init` | `int pthread_mutex_init(pthread_mutex_t *mutex, pthread_mutexattr_t *attr);` | Inicializa um mutex. Normalmente usa-se `attr = NULL` para atributos por omissão. |
| `pthread_mutex_lock` | `int pthread_mutex_lock(pthread_mutex_t *mutex);` | Fecha o trinco: se já estiver fechado, bloqueia até poder adquirir. Garante exclusão mútua. |
| `pthread_mutex_unlock` | `int pthread_mutex_unlock(pthread_mutex_t *mutex);` | Abre o trinco, permitindo que outra thread avance. |
| `pthread_mutex_trylock` | `int pthread_mutex_trylock(pthread_mutex_t *mutex);` | Tenta fechar o trinco **sem bloquear**. Se estiver ocupado, devolve um código de erro (tipicamente `EBUSY`). |
| `pthread_mutex_timedlock` | `int pthread_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *timeout);` | Tenta adquirir o mutex, mas só espera até `timeout`. :contentReference[oaicite:31]{index=31} |

### 5.2 Trincos de leitura-escrita (`pthread_rwlock_*`)

**Cabeçalho:** `#include <pthread.h>` :contentReference[oaicite:32]{index=32}  

| Função | Protótipo (simplificado) | O que faz / notas |
|--------|--------------------------|-------------------|
| `pthread_rwlock_rdlock` | `int pthread_rwlock_rdlock(pthread_rwlock_t *lock);` | Fecha o trinco para **leitura**: vários leitores podem entrar em paralelo, mas nenhum escritor. |
| `pthread_rwlock_wrlock` | `int pthread_rwlock_wrlock(pthread_rwlock_t *lock);` | Fecha o trinco para **escrita**: exclusão total (nem leitores, nem outros escritores). |
| `pthread_rwlock_unlock` | `int pthread_rwlock_unlock(pthread_rwlock_t *lock);` | Abre o trinco, quer estivesse em modo leitura quer em modo escrita. |

Há ainda variantes `pthread_rwlock_tryrdlock`, `pthread_rwlock_trywrlock`, etc. (ver manpages).

### 5.3 Variáveis de condição (`pthread_cond_*`)

**Cabeçalho:** `#include <pthread.h>` :contentReference[oaicite:33]{index=33}  

| Função | Protótipo (simplificado) | O que faz / notas |
|--------|--------------------------|-------------------|
| `pthread_cond_init` | `int pthread_cond_init(pthread_cond_t *cond, pthread_condattr_t *attr);` | Inicializa uma variável de condição. Normalmente `attr = NULL`. |
| `pthread_cond_destroy` | `int pthread_cond_destroy(pthread_cond_t *cond);` | Liberta recursos associados à variável de condição. |
| `pthread_cond_wait` | `int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);` | **Atomically** liberta o `mutex` e bloqueia a thread na fila da condição. Quando acorda, volta a adquirir o `mutex` antes de retornar. |
| `pthread_cond_signal` | `int pthread_cond_signal(pthread_cond_t *cond);` | Acorda **uma** thread bloqueada na condição (se existir). |
| `pthread_cond_broadcast` | `int pthread_cond_broadcast(pthread_cond_t *cond);` | Acorda **todas** as threads bloqueadas na condição. |

**Padrão clássico:**

```c
pthread_mutex_lock(&m);
while (!condicao) {      // usar while, não if
    pthread_cond_wait(&c, &m);
}
/* ... secção crítica ... */
pthread_mutex_unlock(&m);
