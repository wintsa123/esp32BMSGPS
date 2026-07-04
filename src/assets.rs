#![allow(dead_code)]

pub const INDEX_HTML: &[u8] = include_bytes!("web/index.html");
pub const INDEX_HTML_LEN: usize = INDEX_HTML.len();
pub const INDEX_HTML_CONTENT_TYPE: &str = "text/html; charset=utf-8";
