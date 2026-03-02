// temporarily change directory and block other threads from doing so
// useful for simulating mknodat on ios, dealing with long unix socket paths, etc
void lock_fchdir(int dirfd);
void unlock_fchdir();
