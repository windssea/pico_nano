#pragma once
struct httpd_req_aux {char *scratch;size_t scratch_size_limit,scratch_cur_size;unsigned req_hdrs_count;};
