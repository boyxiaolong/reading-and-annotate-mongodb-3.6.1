//
// detail/reactive_socket_send_op.hpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2017 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef ASIO_DETAIL_REACTIVE_SOCKET_SEND_OP_HPP
#define ASIO_DETAIL_REACTIVE_SOCKET_SEND_OP_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "asio/detail/config.hpp"
#include "asio/detail/bind_handler.hpp"
#include "asio/detail/buffer_sequence_adapter.hpp"
#include "asio/detail/fenced_block.hpp"
#include "asio/detail/memory.hpp"
#include "asio/detail/reactor_op.hpp"
#include "asio/detail/socket_ops.hpp"

#include "asio/detail/push_options.hpp"

namespace asio {
namespace detail {

//accept对应的新链接epoll事件注册流程:reactive_socket_service_base::start_accept_op->reactive_socket_service_base::start_op
//读数据epoll事件注册流程:reactive_descriptor_service::async_read_some->reactive_descriptor_service::start_op->epoll_reactor::start_op
//写数据epoll事件注册流程:reactive_descriptor_service::async_write_some->reactive_descriptor_service::start_op->epoll_reactor::start_op
//EPOLL对应网络事件回调：reactive_socket_accept_op_base(新连接) reactive_socket_recv_op_base(读) reactive_socket_send_op_base(写)
//operation分类:reactor_op(网络IO事件处理任务)	completion_handler(全局任务) descriptor_state(reactor_op对应的网络IO事件任务最终加入到该结构中由epoll触发处理)


template <typename ConstBufferSequence>
//reactive_socket_service_base::async_send中构造使用，reactive_socket_send_op基础该类
class reactive_socket_send_op_base : public reactor_op
{
public:
  reactive_socket_send_op_base(socket_type socket,
      socket_ops::state_type state, const ConstBufferSequence& buffers,
      socket_base::message_flags flags, func_type complete_func)
    : reactor_op(&reactive_socket_send_op_base::do_perform, complete_func),
      socket_(socket),
      state_(state),
      buffers_(buffers),
      flags_(flags)
  {
  }

  static status do_perform(reactor_op* base)
  {
    reactive_socket_send_op_base* o(
        static_cast<reactive_socket_send_op_base*>(base));

    buffer_sequence_adapter<asio::const_buffer,
        ConstBufferSequence> bufs(o->buffers_);

    status result = socket_ops::non_blocking_send(o->socket_,
          bufs.buffers(), bufs.count(), o->flags_,
          o->ec_, o->bytes_transferred_) ? done : not_done;

    if (result == done)
      if ((o->state_ & socket_ops::stream_oriented) != 0)
        if (o->bytes_transferred_ < bufs.total_size())
          result = done_and_exhausted;

    ASIO_HANDLER_REACTOR_OPERATION((*o, "non_blocking_send",
          o->ec_, o->bytes_transferred_));

    return result;
  }

private:
  //fd
  socket_type socket_;
  socket_ops::state_type state_;
  //发送的数据在该buffer中
  ConstBufferSequence buffers_;
  socket_base::message_flags flags_;
};

template <typename ConstBufferSequence, typename Handler>
class reactive_socket_send_op :
  public reactive_socket_send_op_base<ConstBufferSequence>
{
public:
  ASIO_DEFINE_HANDLER_PTR(reactive_socket_send_op);

  reactive_socket_send_op(socket_type socket,
      socket_ops::state_type state, const ConstBufferSequence& buffers,
      socket_base::message_flags flags, Handler& handler)
    : reactive_socket_send_op_base<ConstBufferSequence>(socket,
        state, buffers, flags, &reactive_socket_send_op::do_complete),
      handler_(ASIO_MOVE_CAST(Handler)(handler))
  {
    handler_work<Handler>::start(handler_);
  }

  static void do_complete(void* owner, operation* base,
      const asio::error_code& /*ec*/,
      std::size_t /*bytes_transferred*/)
  {
    // Take ownership of the handler object.
    reactive_socket_send_op* o(static_cast<reactive_socket_send_op*>(base));
    ptr p = { asio::detail::addressof(o->handler_), o, o };
    handler_work<Handler> w(o->handler_);

    ASIO_HANDLER_COMPLETION((*o));

    // Make a copy of the handler so that the memory can be deallocated before
    // the upcall is made. Even if we're not about to make an upcall, a
    // sub-object of the handler may be the true owner of the memory associated
    // with the handler. Consequently, a local copy of the handler is required
    // to ensure that any owning sub-object remains valid until after we have
    // deallocated the memory here.
    detail::binder2<Handler, asio::error_code, std::size_t>
      handler(o->handler_, o->ec_, o->bytes_transferred_);
    p.h = asio::detail::addressof(handler.handler_);
    p.reset();

    // Make the upcall if required.
    if (owner)
    {
      fenced_block b(fenced_block::half);
      ASIO_HANDLER_INVOCATION_BEGIN((handler.arg1_, handler.arg2_));
      w.complete(handler, handler.handler_);
      ASIO_HANDLER_INVOCATION_END;
    }
  }

private:
  Handler handler_;
};

} // namespace detail
} // namespace asio

#include "asio/detail/pop_options.hpp"

#endif // ASIO_DETAIL_REACTIVE_SOCKET_SEND_OP_HPP
