
namespace perception {
namespace window {

// Represents the buffer that is sent to the delegate to draw the contents of a
// window.
struct WindowDrawBuffer {
  // The width of the buffer in pixels.
  int width;

  // The height of the buffer in pixels.
  int height;

  // The raw pixel data. 32-bits per pixel BGRA.
  void* pixel_data;

  // Whether the contents of the buffer have been preserved
  // from the previous call. If this is false, then only the part of the image
  // that has changed has to be redrawn into `pixel_data`.
  bool has_preserved_contents_from_previous_draw;
};

}  // namespace window
}  // namespace perception